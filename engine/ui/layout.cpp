// The UI's layout: a small flexbox (rows and columns with gap, padding, margins, grow and
// shrink, alignment, wrapping), a grid of px / auto / fr columns, stacks (children on top of each
// other) and absolutely placed children, with text measured by ui/text.h.
#include <algorithm>
#include <cmath>

#include "ui/ui.h"

namespace cc {
namespace ui {

struct Size {
    float w = 0, h = 0;
};

struct LayoutEngine {
    std::deque<Node>& N;
    explicit LayoutEngine(std::deque<Node>& n) : N(n) {}

    static float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
    static bool finite(float v) { return !std::isnan(v) && v < kInf * 0.5f; }

    template <typename F>
    void children(const Node& n, F f) {
        for (int c = n.first; c >= 0; c = N[size_t(c)].next) f(c, N[size_t(c)]);
    }

    // A definite border-box width (px, or percent of the parent's content width).
    float definiteW(const Node& c, float parentCW) const {
        if (!std::isnan(c.s.w)) return clampf(c.s.w, c.s.minW, c.s.maxW);
        if (!std::isnan(c.s.wPct) && finite(parentCW)) return clampf(c.s.wPct * parentCW / 100.0f, c.s.minW, c.s.maxW);
        return kAuto;
    }
    float definiteH(const Node& c, float parentCH) const {
        if (!std::isnan(c.s.h)) return clampf(c.s.h, c.s.minH, c.s.maxH);
        if (!std::isnan(c.s.hPct) && finite(parentCH)) return clampf(c.s.hPct * parentCH / 100.0f, c.s.minH, c.s.maxH);
        return kAuto;
    }
    static float frameH(const Style& s) { return s.pad.h() + s.border.h(); }
    static float frameV(const Style& s) { return s.pad.v() + s.border.v(); }

    void layoutTextFor(Node& n, float cw) {
        if (n.spans.empty()) return;
        const bool wrap = !n.s.nowrap;
        const float w = finite(cw) ? std::max(0.0f, cw) : kInf;
        if (n.layoutW >= 0 && std::fabs(n.layoutW - w) < 0.01f) return;
        TextOptions o;
        o.maxWidth = w;
        o.wrap = wrap;
        o.ellipsis = n.s.ellipsis;
        o.maxLines = n.s.maxLines;
        layoutText(n.spans, o, n.layout);
        n.layoutW = w;
    }

    // ---- intrinsic widths

    float maxContent(int ci) {
        Node& c = N[size_t(ci)];
        if (c.maxContent >= 0) return c.maxContent;
        const Size s = measure(ci, kInf, kInf);
        c.maxContent = s.w;
        return s.w;
    }

    float minContent(int ci) {
        Node& c = N[size_t(ci)];
        if (c.minContent >= 0) return c.minContent;
        const Style& s = c.s;
        float v;
        const float dw = definiteW(c, kInf);
        if (!std::isnan(dw)) {
            v = dw;
        } else if (s.ellipsis || s.clip || s.scrollY) {
            v = s.minW;
        } else if (!c.spans.empty()) {
            if (s.nowrap) {
                v = maxContent(ci);
            } else {
                TextLayout L;
                TextOptions o;
                o.maxWidth = 0.001f;
                o.breakWords = false;
                layoutText(c.spans, o, L);
                v = L.width + frameH(s);
            }
        } else {
            float m = 0, sum = 0;
            int n = 0;
            children(c, [&](int k, Node& kn) {
                if (kn.s.absolute) return;
                const float x = minContent(k) + kn.s.mar.h();
                m = std::max(m, x);
                sum += x;
                ++n;
            });
            if (s.dir == Dir::Row && !s.wrap) v = sum + s.gap * float(std::max(0, n - 1));
            else if (s.dir == Dir::Grid) {
                // Fixed columns at their size, others at the widest cell's minimum.
                v = 0;
                const size_t nc = std::max<size_t>(1, s.cols.size());
                std::vector<float> col(nc, 0.0f);
                size_t i = 0;
                children(c, [&](int k, Node& kn) {
                    if (kn.s.absolute) return;
                    col[i % nc] = std::max(col[i % nc], minContent(k) + kn.s.mar.h());
                    ++i;
                });
                for (size_t k = 0; k < nc; ++k) v += k < s.cols.size() && s.cols[k].kind == Track::Px ? s.cols[k].v : col[k];
                v += s.gap * float(nc - 1);
            } else v = m;
            v += frameH(s);
        }
        v = clampf(v, s.minW, s.maxW);
        c.minContent = v;
        return v;
    }

    // ---- measuring: the border-box size when at most `availW` wide (shrink to fit, or the
    // definite width), `parentCW` the parent's content width (percentages).

    Size measure(int ni, float availW, float parentCW) {
        Node& n = N[size_t(ni)];
        if (n.mAvail == availW && n.mAvail >= -1) return Size{n.mW, n.mH};
        const Style& s = n.s;
        const float W = definiteW(n, parentCW);
        const float fh = frameH(s), fv = frameV(s);
        const float cw = std::max(0.0f, (std::isnan(W) ? availW : W) - fh);
        const Size c = contentSize(ni, cw);
        float w = std::isnan(W) ? c.w + fh : W;
        w = clampf(w, s.minW, s.maxW);
        const float H = definiteH(n, kAuto);
        float h = std::isnan(H) ? c.h + fv : H;
        h = clampf(h, s.minH, s.maxH);
        n.mAvail = availW;
        n.mW = w;
        n.mH = h;
        return Size{w, h};
    }

    Size contentSize(int ni, float cw) {
        Node& n = N[size_t(ni)];
        const Style& s = n.s;
        if (!n.spans.empty()) {
            layoutTextFor(n, cw);
            float w = n.layout.width;
            if (s.nowrap && !s.ellipsis) w = n.layout.naturalWidth;
            return Size{w, n.layout.height};
        }
        switch (s.dir) {
            case Dir::Column: {
                float w = 0, h = 0;
                int k = 0;
                children(n, [&](int ci, Node& c) {
                    if (c.s.absolute) return;
                    const Size cs = measure(ci, cw - c.s.mar.h(), cw);
                    w = std::max(w, cs.w + c.s.mar.h());
                    h += cs.h + c.s.mar.v();
                    ++k;
                });
                h += s.gap * float(std::max(0, k - 1));
                return Size{w, h};
            }
            case Dir::Row: {
                std::vector<int> items;
                std::vector<float> ws, hs;
                rowSizes(ni, cw, items, ws, hs);
                if (!s.wrap || !finite(cw)) {
                    float w = 0, h = 0;
                    for (size_t i = 0; i < items.size(); ++i) {
                        const Node& c = N[size_t(items[i])];
                        w += ws[i] + c.s.mar.h();
                        h = std::max(h, hs[i] + c.s.mar.v());
                    }
                    w += s.gap * float(std::max<size_t>(1, items.size()) - 1);
                    return Size{w, h};
                }
                // Wrapped rows: lines of items at their own widths.
                float x = 0, lineH = 0, h = 0, w = 0;
                const float rg = s.rowGap >= 0 ? s.rowGap : s.gap;
                bool firstLine = true;
                for (size_t i = 0; i < items.size(); ++i) {
                    const Node& c = N[size_t(items[i])];
                    const float iw = ws[i] + c.s.mar.h();
                    if (x > 0 && x + iw > cw + 0.01f) {
                        h += lineH + (firstLine ? 0.0f : rg);
                        firstLine = false;
                        w = std::max(w, x - s.gap);
                        x = 0;
                        lineH = 0;
                    }
                    x += iw + s.gap;
                    lineH = std::max(lineH, hs[i] + c.s.mar.v());
                }
                if (!items.empty()) {
                    h += lineH + (firstLine ? 0.0f : rg);
                    w = std::max(w, x - s.gap);
                }
                return Size{w, h};
            }
            case Dir::Grid: {
                std::vector<float> colW, rowH;
                gridSizes(ni, cw, colW, rowH);
                float w = 0, h = 0;
                for (float v : colW) w += v;
                for (float v : rowH) h += v;
                const float rg = s.rowGap >= 0 ? s.rowGap : s.gap;
                w += s.gap * float(std::max<size_t>(1, colW.size()) - 1);
                h += rg * float(std::max<size_t>(1, rowH.size()) - 1);
                return Size{w, h};
            }
            case Dir::Stack: {
                float w = 0, h = 0;
                children(n, [&](int ci, Node& c) {
                    if (c.s.absolute) return;
                    const Size cs = measure(ci, cw - c.s.mar.h(), cw);
                    w = std::max(w, cs.w + c.s.mar.h());
                    h = std::max(h, cs.h + c.s.mar.v());
                });
                return Size{w, h};
            }
        }
        return Size{};
    }

    // A row's item widths in `cw` (grow and shrink) and their heights at those widths.
    void rowSizes(int ni, float cw, std::vector<int>& items, std::vector<float>& ws, std::vector<float>& hs) {
        const Node& n = N[size_t(ni)];
        const Style& s = n.s;
        items.clear();
        children(n, [&](int ci, Node& c) {
            if (!c.s.absolute) items.push_back(ci);
        });
        const size_t k = items.size();
        ws.assign(k, 0.0f);
        hs.assign(k, 0.0f);
        std::vector<float> basis(k);
        float used = s.gap * float(k > 0 ? k - 1 : 0);
        for (size_t i = 0; i < k; ++i) {
            const Node& c = N[size_t(items[i])];
            float b = definiteW(c, cw);
            if (std::isnan(b)) b = !std::isnan(c.s.basis) ? c.s.basis : maxContent(items[i]);
            b = clampf(b, c.s.minW, c.s.maxW);
            basis[i] = b;
            used += b + c.s.mar.h();
        }
        ws = basis;
        if (finite(cw) && !s.wrap) {
            float free = cw - used;
            if (free > 0.01f) {
                float g = 0;
                for (size_t i = 0; i < k; ++i) g += N[size_t(items[i])].s.grow;
                if (g > 0)
                    for (size_t i = 0; i < k; ++i) {
                        const Node& c = N[size_t(items[i])];
                        ws[i] = clampf(basis[i] + free * c.s.grow / g, c.s.minW, c.s.maxW);
                    }
            } else if (free < -0.01f) {
                std::vector<bool> frozen(k, false);
                for (int pass = 0; pass < 4 && free < -0.01f; ++pass) {
                    float wsum = 0;
                    for (size_t i = 0; i < k; ++i)
                        if (!frozen[i]) wsum += N[size_t(items[i])].s.shrink * ws[i];
                    if (wsum <= 0) break;
                    float removed = 0;
                    for (size_t i = 0; i < k; ++i) {
                        if (frozen[i]) continue;
                        const Node& c = N[size_t(items[i])];
                        const float want = ws[i] + free * (c.s.shrink * ws[i]) / wsum;
                        const float mn = minContent(items[i]);
                        if (want < mn) {
                            removed += ws[i] - mn;
                            ws[i] = mn;
                            frozen[i] = true;
                        } else {
                            removed += ws[i] - want;
                            ws[i] = want;
                        }
                    }
                    free += removed;
                }
            }
        }
        for (size_t i = 0; i < k; ++i) {
            Node& c = N[size_t(items[i])];
            const Size cs = measure(items[i], ws[i], cw);
            hs[i] = cs.h;
            (void)c;
        }
    }

    void gridSizes(int ni, float cw, std::vector<float>& colW, std::vector<float>& rowH) {
        const Node& n = N[size_t(ni)];
        const Style& s = n.s;
        const size_t nc = std::max<size_t>(1, s.cols.size());
        std::vector<int> cells;
        children(n, [&](int ci, Node& c) {
            if (!c.s.absolute) cells.push_back(ci);
        });
        colW.assign(nc, 0.0f);
        std::vector<float> autoW(nc, 0.0f);
        for (size_t i = 0; i < cells.size(); ++i) {
            const Node& c = N[size_t(cells[i])];
            const size_t col = i % nc;
            autoW[col] = std::max(autoW[col], maxContent(cells[i]) + c.s.mar.h());
        }
        float fixed = s.gap * float(nc - 1), fr = 0;
        for (size_t k = 0; k < nc; ++k) {
            const Track t = k < s.cols.size() ? s.cols[k] : Track::fr(1);
            if (t.kind == Track::Px) colW[k] = t.v;
            else if (t.kind == Track::Auto) colW[k] = autoW[k];
            else fr += t.v;
            if (t.kind != Track::Fr) fixed += colW[k];
        }
        if (fr > 0) {
            const float rest = finite(cw) ? std::max(0.0f, cw - fixed) : 0.0f;
            for (size_t k = 0; k < nc; ++k) {
                const Track t = k < s.cols.size() ? s.cols[k] : Track::fr(1);
                if (t.kind == Track::Fr) colW[k] = finite(cw) ? rest * t.v / fr : autoW[k];
            }
        } else if (finite(cw) && fixed > cw) {
            // Auto columns give up room down to their minimum (a table in a narrow box).
            float over = fixed - cw;
            for (size_t k = 0; k < nc && over > 0; ++k) {
                const Track t = k < s.cols.size() ? s.cols[k] : Track::fr(1);
                if (t.kind != Track::Auto) continue;
                float mn = 0;
                for (size_t i = k; i < cells.size(); i += nc) mn = std::max(mn, minContent(cells[i]) + N[size_t(cells[i])].s.mar.h());
                const float give = std::min(over, std::max(0.0f, colW[k] - mn));
                colW[k] -= give;
                over -= give;
            }
        }
        const size_t nr = (cells.size() + nc - 1) / nc;
        rowH.assign(nr, 0.0f);
        for (size_t i = 0; i < cells.size(); ++i) {
            const Node& c = N[size_t(cells[i])];
            const Size cs = measure(cells[i], colW[i % nc] - c.s.mar.h(), colW[i % nc]);
            rowH[i / nc] = std::max(rowH[i / nc], cs.h + c.s.mar.v());
        }
    }

    float baselineOf(int ci, float w) {
        Node& c = N[size_t(ci)];
        if (!c.spans.empty()) {
            layoutTextFor(c, w - frameH(c.s));
            if (c.layout.lines.empty()) return c.s.pad.t + c.s.border.t;
            return c.s.pad.t + c.s.border.t + c.layout.lines[0].baseline;
        }
        if (c.first >= 0 && c.s.dir != Dir::Grid) {
            // The first child's baseline (rows: the tallest baseline is close enough).
            float best = -1;
            children(c, [&](int k, Node& kn) {
                if (kn.s.absolute || (best >= 0 && c.s.dir == Dir::Column)) return;
                const float b = baselineOf(k, std::max(0.0f, w - frameH(c.s))) + kn.s.mar.t;
                best = std::max(best, b);
            });
            if (best >= 0) return c.s.pad.t + c.s.border.t + best;
        }
        return measure(ci, w, w).h;
    }

    // ---- arranging

    void arrange(int ni, const Rect& r) {
        Node& n = N[size_t(ni)];
        const Style& s = n.s;
        n.rect = r;
        const Rect cb = r.inset(s.border.l + s.pad.l, s.border.t + s.pad.t, s.border.r + s.pad.r, s.border.b + s.pad.b);
        if (!n.spans.empty()) {
            layoutTextFor(n, cb.w);
            n.baseline = s.pad.t + s.border.t + (n.layout.lines.empty() ? 0.0f : n.layout.lines[0].baseline);
        }
        const float scroll = s.scrollY ? n.scroll : 0.0f;
        float extent = 0;
        switch (s.dir) {
            case Dir::Column: extent = arrangeColumn(ni, cb, scroll); break;
            case Dir::Row: extent = arrangeRow(ni, cb, scroll); break;
            case Dir::Grid: extent = arrangeGrid(ni, cb, scroll); break;
            case Dir::Stack: extent = arrangeStack(ni, cb, scroll); break;
        }
        n.contentH = extent;
        // Absolutely placed children: in the padding box.
        const Rect pb = r.inset(s.border.l, s.border.t, s.border.r, s.border.b);
        children(n, [&](int ci, Node& c) {
            if (!c.s.absolute) return;
            const Style& cs = c.s;
            float w = definiteW(c, pb.w);
            float h = definiteH(c, pb.h);
            const bool hasL = !std::isnan(cs.left) || !std::isnan(cs.leftPct), hasR = !std::isnan(cs.right);
            const bool hasT = !std::isnan(cs.top) || !std::isnan(cs.topPct), hasB = !std::isnan(cs.bottom);
            const float L = !std::isnan(cs.leftPct) ? pb.w * cs.leftPct / 100.0f : (std::isnan(cs.left) ? 0.0f : cs.left);
            const float T = !std::isnan(cs.topPct) ? pb.h * cs.topPct / 100.0f : (std::isnan(cs.top) ? 0.0f : cs.top);
            if (std::isnan(w)) {
                if (hasL && hasR) w = std::max(0.0f, pb.w - L - cs.right - cs.mar.h());
                else w = measure(ci, pb.w - cs.mar.h(), pb.w).w;
            }
            if (std::isnan(h)) {
                if (hasT && hasB) h = std::max(0.0f, pb.h - T - cs.bottom - cs.mar.v());
                else h = measure(ci, w, pb.w).h;
                if (!c.spans.empty() || c.first >= 0) h = clampf(h, cs.minH, cs.maxH);
            }
            float x, y;
            if (hasL) x = pb.x + L + cs.mar.l - cs.anchorX * w;
            else if (hasR) x = pb.right() - cs.right - cs.mar.r - w;
            else x = pb.x + cs.mar.l;
            if (hasT) y = pb.y + T + cs.mar.t - cs.anchorY * h;
            else if (hasB) y = pb.bottom() - cs.bottom - cs.mar.b - h;
            else y = pb.y + cs.mar.t;
            arrange(ci, Rect{x, y + cs.offsetY, w, h});
        });
    }

    float arrangeColumn(int ni, const Rect& cb, float scroll) {
        Node& n = N[size_t(ni)];
        const Style& s = n.s;
        std::vector<int> items;
        std::vector<float> ws, hs;
        float total = 0, grow = 0;
        children(n, [&](int ci, Node& c) {
            if (c.s.absolute) return;
            const Align al = c.s.self != Align::Auto ? c.s.self : s.align;
            const float avail = cb.w - c.s.mar.h();
            float w = definiteW(c, cb.w);
            const Size m = measure(ci, std::isnan(w) ? avail : w, cb.w);
            if (std::isnan(w)) w = al == Align::Stretch ? clampf(avail, c.s.minW, c.s.maxW) : std::min(m.w, std::max(avail, c.s.minW));
            float h = m.h;
            if (al == Align::Stretch && std::fabs(w - m.w) > 0.01f && std::isnan(definiteH(c, cb.h))) h = measure(ci, w, cb.w).h;
            items.push_back(ci);
            ws.push_back(w);
            hs.push_back(h);
            total += h + c.s.mar.v();
            grow += c.s.grow;
        });
        total += s.gap * float(items.empty() ? 0 : items.size() - 1);
        float extra = cb.h - total;
        if (extra > 0.01f && grow > 0 && !s.scrollY) {
            for (size_t i = 0; i < items.size(); ++i) {
                const Node& c = N[size_t(items[i])];
                hs[i] = clampf(hs[i] + extra * c.s.grow / grow, c.s.minH, c.s.maxH);
            }
            extra = 0;
        }
        float y = cb.y - scroll, between = s.gap;
        if (extra > 0 && !s.scrollY) {
            if (s.justify == Justify::Center) y += extra * 0.5f;
            else if (s.justify == Justify::End) y += extra;
            else if (s.justify == Justify::SpaceBetween && items.size() > 1) between += extra / float(items.size() - 1);
        }
        for (size_t i = 0; i < items.size(); ++i) {
            const Node& c = N[size_t(items[i])];
            const Align al = c.s.self != Align::Auto ? c.s.self : s.align;
            float x = cb.x + c.s.mar.l;
            const float room = cb.w - c.s.mar.h() - ws[i];
            if (al == Align::Center) x += room * 0.5f;
            else if (al == Align::End) x += room;
            arrange(items[i], Rect{x, y + c.s.mar.t + c.s.offsetY, ws[i], hs[i]});
            y += hs[i] + c.s.mar.v() + between;
        }
        return total;
    }

    float arrangeRow(int ni, const Rect& cb, float scroll) {
        Node& n = N[size_t(ni)];
        const Style& s = n.s;
        std::vector<int> items;
        std::vector<float> ws, hs;
        rowSizes(ni, cb.w, items, ws, hs);
        if (s.wrap) {
            // Lines of items; each line's items aligned in its height.
            const float rg = s.rowGap >= 0 ? s.rowGap : s.gap;
            float y = cb.y - scroll;
            size_t i = 0;
            float extent = 0;
            while (i < items.size()) {
                size_t j = i;
                float x = 0, lineH = 0;
                while (j < items.size()) {
                    const Node& c = N[size_t(items[j])];
                    const float iw = ws[j] + c.s.mar.h();
                    if (j > i && x + iw > cb.w + 0.01f) break;
                    x += iw + s.gap;
                    lineH = std::max(lineH, hs[j] + c.s.mar.v());
                    ++j;
                }
                float lx = cb.x, lineGap = s.gap;
                const float free = cb.w - (x - s.gap);
                if (s.justify == Justify::Center) lx += free * 0.5f;
                else if (s.justify == Justify::End) lx += free;
                else if (s.justify == Justify::SpaceBetween && j - i > 1 && free > 0) lineGap += free / float(j - i - 1);
                for (size_t k = i; k < j; ++k) {
                    const Node& c = N[size_t(items[k])];
                    float yy = y + c.s.mar.t;
                    const Align al = c.s.self != Align::Auto ? c.s.self : s.align;
                    float h = hs[k];
                    if (al == Align::Stretch && std::isnan(definiteH(c, kAuto))) h = lineH - c.s.mar.v();
                    else if (al == Align::Center) yy += (lineH - c.s.mar.v() - h) * 0.5f;
                    else if (al == Align::End) yy += lineH - c.s.mar.v() - h;
                    arrange(items[k], Rect{lx + c.s.mar.l, yy + c.s.offsetY, ws[k], h});
                    lx += ws[k] + c.s.mar.h() + lineGap;
                }
                y += lineH + rg;
                extent += lineH + (i > 0 ? rg : 0.0f);
                i = j;
            }
            return extent;
        }
        float lineH = 0, used = 0, grow = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            const Node& c = N[size_t(items[i])];
            lineH = std::max(lineH, hs[i] + c.s.mar.v());
            used += ws[i] + c.s.mar.h();
            grow += c.s.grow;
        }
        used += s.gap * float(items.empty() ? 0 : items.size() - 1);
        const float useH = cb.h > 0 ? cb.h : lineH;
        // Baselines.
        float maxBase = 0;
        std::vector<float> bases(items.size(), 0.0f);
        if (s.align == Align::Baseline)
            for (size_t i = 0; i < items.size(); ++i) {
                bases[i] = baselineOf(items[i], ws[i]) + N[size_t(items[i])].s.mar.t;
                maxBase = std::max(maxBase, bases[i]);
            }
        float x = cb.x, between = s.gap;
        const float free = cb.w - used;
        if (free > 0.01f && grow <= 0) {
            if (s.justify == Justify::Center) x += free * 0.5f;
            else if (s.justify == Justify::End) x += free;
            else if (s.justify == Justify::SpaceBetween && items.size() > 1) between += free / float(items.size() - 1);
        }
        for (size_t i = 0; i < items.size(); ++i) {
            const Node& c = N[size_t(items[i])];
            const Align al = c.s.self != Align::Auto ? c.s.self : s.align;
            float y = cb.y - scroll + c.s.mar.t;
            float h = hs[i];
            const float room = useH - c.s.mar.v();
            if (al == Align::Stretch && std::isnan(definiteH(c, cb.h))) h = clampf(room, c.s.minH, c.s.maxH);
            else if (al == Align::Center) y += (room - h) * 0.5f;
            else if (al == Align::End) y += room - h;
            else if (al == Align::Baseline) y += maxBase - bases[i];
            arrange(items[i], Rect{x + c.s.mar.l, y + c.s.offsetY, ws[i], h});
            x += ws[i] + c.s.mar.h() + between;
        }
        return lineH;
    }

    float arrangeGrid(int ni, const Rect& cb, float scroll) {
        Node& n = N[size_t(ni)];
        const Style& s = n.s;
        std::vector<float> colW, rowH;
        gridSizes(ni, cb.w, colW, rowH);
        const size_t nc = colW.size();
        const float rg = s.rowGap >= 0 ? s.rowGap : s.gap;
        std::vector<float> colX(nc, cb.x);
        for (size_t k = 1; k < nc; ++k) colX[k] = colX[k - 1] + colW[k - 1] + s.gap;
        size_t i = 0;
        float y = cb.y - scroll;
        float extent = 0;
        std::vector<float> rowY(rowH.size(), 0.0f);
        for (size_t r = 0; r < rowH.size(); ++r) {
            rowY[r] = y;
            y += rowH[r] + rg;
            extent += rowH[r] + (r > 0 ? rg : 0.0f);
        }
        children(n, [&](int ci, Node& c) {
            if (c.s.absolute) return;
            const size_t col = i % nc, row = i / nc;
            const Align al = c.s.self != Align::Auto ? c.s.self : s.align;
            float w = colW[col] - c.s.mar.h();
            float h = rowH[row] - c.s.mar.v();
            float x = colX[col] + c.s.mar.l, yy = rowY[row] + c.s.mar.t;
            if (al != Align::Stretch) {
                const Size m = measure(ci, w, colW[col]);
                if (al == Align::Center) yy += (h - m.h) * 0.5f;
                else if (al == Align::End) yy += h - m.h;
                h = m.h;
            }
            arrange(ci, Rect{x, yy + c.s.offsetY, w, h});
            ++i;
        });
        return extent;
    }

    float arrangeStack(int ni, const Rect& cb, float scroll) {
        Node& n = N[size_t(ni)];
        const Style& s = n.s;
        float extent = 0;
        children(n, [&](int ci, Node& c) {
            if (c.s.absolute) return;
            const Align al = c.s.self != Align::Auto ? c.s.self : s.align;
            const float availW = cb.w - c.s.mar.h(), availH = cb.h - c.s.mar.v();
            float w = definiteW(c, cb.w);
            const Size m = measure(ci, std::isnan(w) ? availW : w, cb.w);
            if (std::isnan(w)) w = al == Align::Stretch ? availW : std::min(m.w, availW);
            float h = definiteH(c, cb.h);
            if (std::isnan(h)) {
                // Children of a stack fill its height unless the stack centres or ends them.
                const bool fill = s.justify == Justify::Start && al == Align::Stretch && c.s.maxH >= kInf;
                const float mh = measure(ci, w, cb.w).h;
                h = clampf(fill ? std::max(availH, mh) : mh, c.s.minH, c.s.maxH);
            }
            float x = cb.x + c.s.mar.l, y = cb.y + c.s.mar.t - scroll;
            if (al == Align::Center) x += (availW - w) * 0.5f;
            else if (al == Align::End) x += availW - w;
            if (s.justify == Justify::Center) y += (availH - h) * 0.5f;
            else if (s.justify == Justify::End) y += availH - h;
            arrange(ci, Rect{x, y + c.s.offsetY, w, h});
            extent = std::max(extent, h + c.s.mar.v());
        });
        return extent;
    }
};

void Ui::layoutAll() {
    LayoutEngine L(nodes_);
    for (int li = 0; li < int(Layer::Count); ++li) {
        const int r = roots_[li];
        if (r < 0) continue;
        nodes_[size_t(r)].s.w = viewW_;
        nodes_[size_t(r)].s.h = viewH_;
        L.arrange(r, Rect{0, 0, viewW_, viewH_});
    }
}

}  // namespace ui
}  // namespace cc
