// The end of a UI frame: layout, the bookkeeping the next frame's input works with (rects,
// hit order, focusables, scroll limits, caret stops), and painting the tree into the draw list.
#include <algorithm>
#include <cmath>

#include "ui/ui.h"

namespace cc {
namespace ui {

namespace {
Color unpack(uint32_t v) {
    return Color(float((v >> 24) & 255) / 255.0f, float((v >> 16) & 255) / 255.0f, float((v >> 8) & 255) / 255.0f, float(v & 255) / 255.0f);
}
}  // namespace

struct Painter {
    Ui& ui;
    DrawList& dl;
    float sc;

    Rect dev(const Rect& r) const {
        const float x0 = std::round(r.x * sc), y0 = std::round(r.y * sc);
        const float x1 = std::round(r.right() * sc), y1 = std::round(r.bottom() * sc);
        return Rect{x0, y0, x1 - x0, y1 - y0};
    }
    float bw(float w) const { return w > 0 ? std::max(1.0f, std::floor(w * sc + 0.25f)) : 0.0f; }

    void node(int ni, float opacity) {
        Node& n = ui.nodes_[size_t(ni)];
        const Style& s = n.s;
        const float op = opacity * s.opacity;
        if (op <= 0.003f) return;
        const Rect box = n.rect;
        const Rect d = dev(box);
        float rad[4];
        for (int i = 0; i < 4; ++i) rad[i] = s.radius[i] * sc;
        for (int i = 0; i < s.shadowCount; ++i) {
            const Shadow& sh = s.shadows[i];
            dl.shadow(d, sh.dx * sc, sh.dy * sc, sh.color.withAlpha(op), sh.blur * sc, sh.spread * sc, rad, true);
        }
        if (s.backdropBlur > 0) {
            dl.capture(s.backdropBlur * sc);
            dl.backdrop(d, rad);
        }
        const bool uniform = s.border.l == s.border.t && s.border.t == s.border.r && s.border.r == s.border.b;
        if (s.grad != GradNone) {
            dl.gradient(d, s.bg.withAlpha(op), s.bg2.withAlpha(op), s.gradAngle, rad, s.grad);
            if (uniform && s.border.l > 0) dl.rectRadii(d, Color(), rad, bw(s.border.l), s.borderColor.withAlpha(op));
        } else if (uniform) {
            dl.rectRadii(d, s.bg.withAlpha(op), rad, bw(s.border.l), s.borderColor.withAlpha(op));
        } else {
            dl.rectRadii(d, s.bg.withAlpha(op), rad, 0, Color());
        }
        if (!uniform && s.borderColor.visible()) {
            const Color bc = s.borderColor.withAlpha(op);
            if (s.border.t > 0) dl.rect(Rect{d.x, d.y, d.w, bw(s.border.t)}, bc);
            if (s.border.b > 0) dl.rect(Rect{d.x, d.bottom() - bw(s.border.b), d.w, bw(s.border.b)}, bc);
            if (s.border.l > 0) dl.rect(Rect{d.x, d.y, bw(s.border.l), d.h}, bc);
            if (s.border.r > 0) dl.rect(Rect{d.right() - bw(s.border.r), d.y, bw(s.border.r), d.h}, bc);
        }
        if (n.image.valid()) {
            const Rect cb = dev(box.inset(s.border.l + s.pad.l, s.border.t + s.pad.t, s.border.r + s.pad.r, s.border.b + s.pad.b));
            dl.image(cb, n.image, n.imageUv, n.imageTint.withAlpha(op), rad);
        }
        if (!n.spans.empty()) {
            const Rect cb = box.inset(s.border.l + s.pad.l, s.border.t + s.pad.t, s.border.r + s.pad.r, s.border.b + s.pad.b);
            const int align = s.textAlign == TextAlign::Center ? 1 : s.textAlign == TextAlign::Right ? 2 : 0;
            float ty = cb.y;
            if (s.textVCenter) ty += (cb.h - n.layout.height) * 0.5f;
            const bool clipText = s.textOffsetX != 0;
            if (clipText) dl.pushClip(dev(cb));
            // Key caps behind their words.
            for (const TextBox& b : n.layout.boxes) {
                const Span& sp = n.spans[size_t(b.span)];
                if (b.line >= int(n.layout.lines.size())) continue;
                const TextLine& L = n.layout.lines[size_t(b.line)];
                float off = 0;
                if (align == 1) off = (cb.w - L.width) * 0.5f;
                else if (align == 2) off = cb.w - L.width;
                const Face f = faceFor(sp.style.weight, sp.style.mono);
                const Fonts& F = Fonts::get();
                const float a = F.ascent(f, sp.style.size), ds = F.descent(f, sp.style.size);
                const float lh = sp.style.lineHeight > 0 ? sp.style.lineHeight : F.normalLineHeight(f, sp.style.size);
                const float top = ty + L.baseline - a - (lh - a - ds) * 0.5f;
                Rect kb = dev(Rect{cb.x + off + b.x - s.textOffsetX, top, b.w, lh});
                float r4[4];
                for (float& v : r4) v = sp.boxRadius * sc;
                if (sp.boxBg || sp.boxBorder) {
                    dl.rectRadii(kb, unpack(sp.boxBg).withAlpha(op), r4, bw(sp.boxBorderW), unpack(sp.boxBorder).withAlpha(op));
                    if (sp.boxBottomW > sp.boxBorderW) {
                        const float extra = bw(sp.boxBottomW) - bw(sp.boxBorderW);
                        dl.rect(Rect{kb.x + r4[3], kb.bottom() - bw(sp.boxBottomW), kb.w - r4[2] - r4[3], extra}, unpack(sp.boxBorder).withAlpha(op));
                    }
                }
            }
            const Color* shc = nullptr;
            Color shcol;
            if (s.textShadow.color.visible()) {
                shcol = s.textShadow.color.withAlpha(op);
                shc = &shcol;
            }
            drawText(dl, n.spans, n.layout, cb.x - s.textOffsetX, ty, cb.w, align, sc, s.color.withAlpha(op), s.textShadow.blur, shc,
                     s.textShadow.dx, s.textShadow.dy);
            if (clipText) dl.popClip();
        }
        if (n.paint) {
            const size_t before = dl.prims.size();
            n.paint(dl, d, sc);
            // Custom drawing follows the node's opacity.
            if (op < 0.999f)
                for (size_t i = before; i < dl.prims.size(); ++i) {
                    dl.prims[i].fill.a *= op;
                    dl.prims[i].fill2.a *= op;
                    dl.prims[i].border.a *= op;
                }
        }
        const bool clips = s.clip || s.scrollY;
        if (clips) dl.pushClip(dev(box.inset(s.border.l, s.border.t, s.border.r, s.border.b)));
        for (int c = n.first; c >= 0; c = ui.nodes_[size_t(c)].next) node(c, op);
        if (clips) dl.popClip();
        if (s.scrollY) {
            const float viewH = box.h - s.border.v() - s.pad.v();
            if (n.contentH > viewH + 0.5f && viewH > 0) {
                const float track = box.h - s.border.v() - 4.0f;
                const float th = std::max(24.0f, track * viewH / n.contentH);
                const float maxS = n.contentH - viewH;
                const float t = maxS > 0 ? std::clamp(n.scroll / maxS, 0.0f, 1.0f) : 0.0f;
                const Rect bar{box.right() - s.border.r - 7.0f, box.y + s.border.t + 2.0f + (track - th) * t, 5.0f, th};
                dl.rect(dev(bar), rgba(148, 163, 184, 0.38f * op), 2.5f * sc);
            }
        }
        if (s.focusable && s.focusRing && ui.focus_ == n.id && ui.focusVisible_) {
            float r2[4];
            for (int i = 0; i < 4; ++i) r2[i] = rad[i] + 2.0f * sc;
            dl.rectRadii(d.expand(2.0f * sc), Color(), r2, std::max(1.0f, std::round(2.0f * sc)), ui.focusRingColor.withAlpha(op));
        }
    }
};

void Ui::endFrame() {
    // Toast and tooltip.
    if (toastShown()) {
        layer(Layer::Toast);
        Style s;
        s.absolute = true;
        s.leftPct = 50;
        s.anchorX = 0.5f;
        s.bottom = 28;
        s.pad = Edges(10, 16);
        s.round(12);
        s.bg = hex(0x111823);
        s.outline(1, rgba(148, 163, 184, 0.35f));
        s.shadow(0, 10, 30, 0, rgba(0, 0, 0, 0.4f));
        s.maxW = viewW_ * 0.8f;
        s.font(14);
        s.color = hex(0xe8edf4);
        Node& t = text(toast_, s);
        t.label = "toast";
    }
    {
        // Tooltip of the hovered node (last frame), after half a second.
        Id hov = 0;
        std::string tip;
        for (auto it = hitOrder_.rbegin(); it != hitOrder_.rend(); ++it) {
            auto ln = last_.find(*it);
            if (ln == last_.end() || !ln->second.rect.contains(px_, py_) || !ln->second.clip.contains(px_, py_)) continue;
            hov = *it;
            break;
        }
        for (const Node& n : nodes_)
            if (n.id == hov) tip = n.tooltip;
        if (hov != tooltipId_ || tip.empty() || mouseDown_) {
            tooltipId_ = tip.empty() ? 0 : hov;
            tooltipSince_ = time_;
        }
        if (tooltipId_ && time_ - tooltipSince_ > 0.55 && !tip.empty()) {
            layer(Layer::Tooltip);
            Style s;
            s.absolute = true;
            s.left = std::min(px_ + 12.0f, std::max(0.0f, viewW_ - 320.0f));
            s.top = py_ + 18.0f > viewH_ - 60.0f ? py_ - 48.0f : py_ + 18.0f;
            s.maxW = 300;
            s.pad = Edges(6, 9);
            s.round(7);
            s.bg = rgba(12, 16, 23, 0.96f);
            s.outline(1, rgba(148, 163, 184, 0.3f));
            s.shadow(0, 6, 18, 0, rgba(0, 0, 0, 0.4f));
            s.font(12);
            s.color = hex(0xe8edf4);
            s.text.lineHeight = 17;
            text(tip, s);
        }
    }
    layoutAll();
    // Bookkeeping for the next frame's input.
    last_.clear();
    hitOrder_.clear();
    focusables_.clear();
    caretStops_.clear();
    caretX0_.clear();
    int order = 0;
    struct Visit {
        Ui& ui;
        int& order;
        void go(int ni, const Rect& clip, bool inScope) {
            Node& n = ui.nodes_[size_t(ni)];
            inScope = inScope || (ui.focusScope_ && n.id == ui.focusScope_);
            LastNode ln;
            ln.rect = n.rect;
            ln.layer = n.layer;
            ln.clip = clip;
            ln.order = order++;
            auto it = ui.interactiveNow_.find(n.id);
            ln.interactive = it != ui.interactiveNow_.end();
            ln.hit = n.s.hit || ln.interactive;
            ln.scroll = n.s.scrollY;
            const float viewH = n.rect.h - n.s.border.v() - n.s.pad.v();
            ln.maxScroll = std::max(0.0f, n.contentH - viewH);
            if (n.s.scrollY) {
                float& sv = ui.scroll_[n.id];
                sv = std::clamp(sv, 0.0f, ln.maxScroll);
            }
            if (ln.hit && n.parent >= 0) ui.hitOrder_.push_back(n.id);
            const Rect visible = n.rect.intersect(clip);
            if (n.s.focusable && ln.interactive && !visible.empty()) {
                ui.focusables_.push_back(Focusable{n.id, n.rect, n.layer, it->second, n.label});
                if (inScope && !ui.firstInScope_ && !(it->second & InteractText)) ui.firstInScope_ = n.id;
            }
            if (ln.interactive && (it->second & InteractText)) {
                const Rect cb = n.rect.inset(n.s.border.l + n.s.pad.l, n.s.border.t + n.s.pad.t, n.s.border.r + n.s.pad.r,
                                             n.s.border.b + n.s.pad.b);
                ui.caretX0_[n.id] = cb.x;
                // Caret stops by character (the shown text may differ from the edited one).
                auto& stops = ui.caretStops_[n.id];
                float off = 0;
                if (n.s.textAlign == TextAlign::Center && !n.layout.lines.empty()) off = (cb.w - n.layout.lines[0].width) * 0.5f;
                stops.push_back({0u, off - n.s.textOffsetX});
                uint32_t k = 0;
                for (const ShapedGlyph& g : n.layout.glyphs) stops.push_back({++k, off + g.x + g.adv - n.s.textOffsetX});
            }
            if (ln.interactive || ln.hit || ln.scroll || n.s.focusable) ui.last_[n.id] = ln;
            Rect inner = clip;
            if (n.s.clip || n.s.scrollY) inner = clip.intersect(n.rect);
            for (int c = n.first; c >= 0; c = ui.nodes_[size_t(c)].next) go(c, inner, inScope);
        }
    };
    Visit v{*this, order};
    firstInScope_ = 0;
    for (int li = 0; li < int(Layer::Count); ++li)
        if (roots_[li] >= 0) v.go(roots_[li], Rect{0, 0, viewW_, viewH_}, false);
    // Focus: still there? The first one of a new screen?
    bool present = false;
    for (const Focusable& f : focusables_)
        if (f.id == focus_) present = true;
    if (focusFirstPending_ && focusScope_ && firstInScope_) {
        focus_ = firstInScope_;
        present = true;
        focusFirstPending_ = false;
    }
    focusScope_ = 0;
    if (focusFirstPending_) {
        int topLayer = modalLayer_;
        for (const Focusable& f : focusables_) topLayer = std::max(topLayer, f.layer);
        for (const Focusable& f : focusables_)
            if (f.layer == topLayer) {
                focus_ = f.id;
                present = true;
                break;
            }
        focusFirstPending_ = false;
    }
    if (!present) focus_ = 0;
    {
        auto it = interactiveNow_.find(focus_);
        const bool text = focus_ && it != interactiveNow_.end() && (it->second & InteractText);
        if (text != textInputOn_) {
            textInputOn_ = text;
            const Rect r = text ? lastRect(focus_) : Rect{};
            host_->setTextInput(text, Rect{r.x * scale_, r.y * scale_, r.w * scale_, r.h * scale_});
        }
    }
    // Scroll the focused node into view (next frame).
    if (scrollTo_) {
        int target = -1;
        for (size_t i = 0; i < nodes_.size(); ++i)
            if (nodes_[i].id == scrollTo_) target = int(i);
        if (target >= 0) {
            const Rect r = nodes_[size_t(target)].rect;
            for (int p = nodes_[size_t(target)].parent; p >= 0; p = nodes_[size_t(p)].parent) {
                const Node& sn = nodes_[size_t(p)];
                if (!sn.s.scrollY) continue;
                const Rect vb = sn.rect.inset(sn.s.border.l, sn.s.border.t, sn.s.border.r, sn.s.border.b);
                float& sv = scroll_[sn.id];
                if (r.y < vb.y + 4) sv -= vb.y + 4 - r.y;
                else if (r.bottom() > vb.bottom() - 4) sv += r.bottom() - (vb.bottom() - 4);
                sv = std::max(0.0f, sv);
                break;
            }
        }
        scrollTo_ = 0;
    }
    // Cursor.
    {
        Cursor c = Cursor::Arrow;
        const Id h = hitTest(px_, py_, true);
        if (h) {
            for (const Node& n : nodes_)
                if (n.id == h) c = n.s.cursor;
        }
        if (c != cursorSet_) {
            cursorSet_ = c;
            host_->setCursor(c);
        }
    }
    // Paint.
    Fonts::get().beginFrame();
    dl_.clear();
    Painter P{*this, dl_, scale_};
    for (int li = 0; li < int(Layer::Count); ++li)
        if (roots_[li] >= 0) P.node(roots_[li], 1.0f);
}

}  // namespace ui
}  // namespace cc
