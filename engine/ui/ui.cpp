#include "ui/ui.h"

#include <algorithm>
#include <cmath>

namespace cc {
namespace ui {

namespace {

Id hashKey(Id seed, const std::string& key) {
    uint64_t h = seed ^ 0xcbf29ce484222325ull;
    for (unsigned char c : key) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    h ^= h >> 29;
    return h ? h : 1;
}

bool isWordChar(uint32_t c) { return c == '_' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0x80; }

}  // namespace

Ui::Ui() {
    for (int& r : roots_) r = -1;
}

float Ui::chooseScale(int w, int h, float dpr) {
    if (dpr <= 0.0f) dpr = 1.0f;
    const float cssW = float(w) / dpr, cssH = float(h) / dpr;
    float f = std::min(cssW / 1920.0f, cssH / 1080.0f);
    if (f < 1.0f) f = 1.0f;
    if (f > 2.5f) f = 2.5f;
    return dpr * f;
}

// ------------------------------------------------------------------------------------- frame

void Ui::beginFrame(const FrameInput& in) {
    scale_ = in.scale > 0.1f ? in.scale : 1.0f;
    viewW_ = float(in.width) / scale_;
    viewH_ = float(in.height) / scale_;
    time_ = in.time;
    dt_ = in.dt;
    dl_.setViewport(in.width, in.height);
    interactiveLast_.swap(interactiveNow_);
    interactiveNow_.clear();
    lastModalLayer_ = modalLayer_;
    modalLayer_ = -1;
    clicked_.clear();
    rightClicked_.clear();
    doubleClicked_.clear();
    backPressed_ = false;
    for (bool& p : padPressed_) p = false;
    keysFree_.clear();
    textEvents_.clear();
    events_ = in.events;
    sceneKeys_ = in.sceneKeys;
    {
        auto it = interactiveLast_.find(focus_);
        focusIsText_ = focus_ && it != interactiveLast_.end() && (it->second & InteractText);
        focusFlags_ = it != interactiveLast_.end() ? it->second : 0;
    }
    for (const InputEvent& e : in.events) handleEvent(e);
    // Gamepad: d-pad and left stick move the focus (with repeat), A activates, B goes back; not
    // while the controller drives.
    if (in.padConnected && sceneKeys_) {
        for (int b = 0; b < 17; ++b) padHeldPrev_[b] = in.padHeld[b];
        for (float& r : padRepeat_) r = 0;
    } else if (in.padConnected) {
        const float ax = in.padAxis[0], ay = in.padAxis[1];
        bool dir[4] = {in.padHeld[12] || ay < -0.6f, in.padHeld[13] || ay > 0.6f, in.padHeld[14] || ax < -0.6f,
                       in.padHeld[15] || ax > 0.6f};
        for (int d = 0; d < 4; ++d) {
            if (!dir[d]) {
                padRepeat_[d] = 0;
                continue;
            }
            bool fire = false;
            if (padRepeat_[d] == 0) {
                fire = true;
                padRepeat_[d] = 0.45f;
            } else {
                padRepeat_[d] -= in.dt;
                if (padRepeat_[d] <= 0) {
                    fire = true;
                    padRepeat_[d] = 0.11f;
                }
            }
            if (!fire) continue;
            usingPad_ = true;
            static const Key keys[4] = {Key::Up, Key::Down, Key::Left, Key::Right};
            const bool lr = d >= 2;
            if ((lr && (focusFlags_ & InteractArrowsLR)) || (!lr && (focusFlags_ & InteractArrowsUD))) {
                keysFree_.push_back({keys[d], 0});
            } else {
                focusVisible_ = true;
                moveFocus(d == 2 ? -1 : d == 3 ? 1 : 0, d == 0 ? -1 : d == 1 ? 1 : 0);
            }
        }
        for (int b = 0; b < 12; ++b) {
            const bool down = in.padHeld[b];
            if (down && !padHeldPrev_[b]) {
                padPressed_[b] = true;
                usingPad_ = true;
                if (b == 0 && focus_ && !focusIsText_) {
                    clicked_.insert(focus_);
                    focusVisible_ = true;
                }
                if (b == 1) backPressed_ = true;
            }
        }
        for (int b = 0; b < 17; ++b) padHeldPrev_[b] = in.padHeld[b];
    } else {
        for (bool& b : padHeldPrev_) b = false;
    }
    // The pointer's node.
    const Id top = hitTest(px_, py_, false);
    hot_ = hitTest(px_, py_, true);
    wantsPointer_ = top != 0 || active_ != 0 || touchScrolling_;
    // Tooltip: the hovered node with a tooltip, after a moment.
    // (the text is taken from the last frame's nodes in endFrame)
    // Tree.
    nodes_.clear();
    stack_.clear();
    idStack_.assign(1, 0x9e3779b97f4a7c15ull);
    autoCounter_ = 0;
    building_.clear();
    for (int& r : roots_) r = -1;
    layerStack_.clear();
    layer_ = Layer::Base;
    layer(Layer::Base);
}

void Ui::layer(Layer l) {
    layer_ = l;
    const int li = int(l);
    if (roots_[li] < 0) {
        Node n;
        n.id = hashKey(0, "root") + Id(li);
        n.s.dir = Dir::Stack;
        n.s.w = viewW_;
        n.s.h = viewH_;
        n.layer = li;
        roots_[li] = int(nodes_.size());
        nodes_.push_back(std::move(n));
    }
    stack_.assign(1, roots_[li]);
}

void Ui::pushLayer(Layer l) {
    layerStack_.push_back({layer_, stack_});
    layer(l);
}

void Ui::popLayer() {
    if (layerStack_.empty()) return;
    layer_ = layerStack_.back().first;
    stack_ = layerStack_.back().second;
    layerStack_.pop_back();
}

Id Ui::id(const std::string& key) const { return hashKey(idStack_.back(), key); }
void Ui::pushId(const std::string& key) { idStack_.push_back(hashKey(idStack_.back(), key)); }
void Ui::popId() {
    if (idStack_.size() > 1) idStack_.pop_back();
}
Id Ui::autoId() { return hashKey(idStack_.back() ^ 0x51ed27ull, std::to_string(++autoCounter_)); }

Node& Ui::open(Id id, const Style& s) {
    const int parent = stack_.empty() ? roots_[int(layer_)] : stack_.back();
    Node n;
    n.id = id;
    n.s = s;
    n.parent = parent;
    n.layer = int(layer_);
    auto it = interactiveNow_.find(id);
    if (it != interactiveNow_.end() && (it->second & InteractFocusable)) n.s.focusable = true;
    if (it != interactiveNow_.end()) n.s.hit = true;
    auto sc = scroll_.find(id);
    if (sc != scroll_.end()) n.scroll = sc->second;
    const int idx = int(nodes_.size());
    nodes_.push_back(std::move(n));
    if (parent >= 0) {
        Node& p = nodes_[size_t(parent)];
        if (p.last >= 0) nodes_[size_t(p.last)].next = idx;
        else p.first = idx;
        p.last = idx;
    }
    stack_.push_back(idx);
    return nodes_[size_t(idx)];
}

void Ui::close() {
    if (stack_.size() > 1) stack_.pop_back();
}

Node& Ui::leaf(Id id, const Style& s) {
    Node& n = open(id, s);
    close();
    return n;
}

Node& Ui::text(const std::string& t, const Style& s, Id id) {
    Node& n = leaf(id ? id : autoId(), s);
    n.spans.push_back(Span{t, s.text});
    return n;
}

Node& Ui::rich(std::vector<Span> spans, const Style& s, Id id) {
    Node& n = leaf(id ? id : autoId(), s);
    n.spans = std::move(spans);
    return n;
}

void Ui::spacer(float grow) {
    Style s;
    s.grow = grow;
    leaf(s);
}

// ------------------------------------------------------------------------------------- input

Rect Ui::lastRect(Id id) const {
    auto it = last_.find(id);
    return it == last_.end() ? Rect{} : it->second.rect;
}

Id Ui::hitTest(float x, float y, bool interactiveOnly) const {
    for (auto it = hitOrder_.rbegin(); it != hitOrder_.rend(); ++it) {
        auto ln = last_.find(*it);
        if (ln == last_.end()) continue;
        const LastNode& n = ln->second;
        if (lastModalLayer_ >= 0 && n.layer < lastModalLayer_) {
            // Below a modal layer: blocked (the modal's backdrop takes the pointer).
            return 0;
        }
        if (!n.rect.contains(x, y) || !n.clip.contains(x, y)) continue;
        if (!interactiveOnly) return *it;
        return n.interactive ? *it : 0;
    }
    if (lastModalLayer_ >= 0 && !interactiveOnly) return 1;  // the modal's backdrop
    return 0;
}

std::vector<Id> Ui::scrollersAt(float x, float y) const {
    std::vector<std::pair<int, Id>> v;
    for (const auto& kv : last_) {
        const LastNode& n = kv.second;
        if (!n.scroll || n.maxScroll <= 0) continue;
        if (lastModalLayer_ >= 0 && n.layer < lastModalLayer_) continue;
        if (n.rect.contains(x, y) && n.clip.contains(x, y)) v.push_back({n.order, kv.first});
    }
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });  // innermost first
    std::vector<Id> out;
    for (auto& p : v) out.push_back(p.second);
    return out;
}

void Ui::setFocus(Id id, bool visible) {
    focus_ = id;
    if (visible) focusVisible_ = true;
}

void Ui::handleEvent(const InputEvent& e) {
    switch (e.type) {
        case InputEvent::MouseMove:
            px_ = e.x / scale_;
            py_ = e.y / scale_;
            break;
        case InputEvent::TouchDown:
        case InputEvent::MouseDown: {
            px_ = e.x / scale_;
            py_ = e.y / scale_;
            if (e.type == InputEvent::TouchDown) {
                touchActive_ = true;
                touchScrolling_ = false;
                touchX0_ = px_;
                touchY0_ = touchLastY_ = py_;
            }
            if (e.button != 0) break;
            usingPad_ = false;
            mouseDown_ = true;
            const Id h = hitTest(px_, py_, true);
            if (h) {
                active_ = h;
                auto it = interactiveLast_.find(h);
                const unsigned fl = it != interactiveLast_.end() ? it->second : 0;
                if (fl & InteractFocusable) {
                    focus_ = h;
                    focusVisible_ = false;
                    focusIsText_ = (fl & InteractText) != 0;
                    focusFlags_ = fl;
                }
                if (lastClickId_ == h && time_ - lastClickTime_ < 0.45) doubleClicked_.insert(h);
                lastClickId_ = h;
                lastClickTime_ = time_;
                if (focusIsText_ && focus_ == h) {
                    InputEvent m = e;
                    textEvents_.push_back(m);
                }
            } else {
                // A click elsewhere takes the focus away from a text field.
                if (focusIsText_) {
                    focus_ = 0;
                    focusIsText_ = false;
                    focusFlags_ = 0;
                }
            }
            break;
        }
        case InputEvent::TouchMove:
            if (touchActive_) {
                const float x = e.x / scale_, y = e.y / scale_;
                if (!touchScrolling_ && std::fabs(y - touchY0_) > 8.0f) {
                    const std::vector<Id> s = scrollersAt(touchX0_, touchY0_);
                    if (!s.empty()) {
                        touchScrolling_ = true;
                        touchScroller_ = s[0];
                        active_ = 0;
                    }
                }
                if (touchScrolling_) {
                    scroll_[touchScroller_] -= (y - touchLastY_);
                    touchLastY_ = y;
                    break;
                }
                px_ = x;
                py_ = y;
            }
            break;
        case InputEvent::TouchUp:
        case InputEvent::MouseUp: {
            px_ = e.x / scale_;
            py_ = e.y / scale_;
            if (e.type == InputEvent::TouchUp) {
                touchActive_ = false;
                if (touchScrolling_) {
                    touchScrolling_ = false;
                    mouseDown_ = false;
                    active_ = 0;
                    px_ = py_ = -1;  // no hover left behind on a touch screen
                    break;
                }
            }
            if (e.button != 0) {
                if (e.button == 2) {
                    const Id h = hitTest(px_, py_, true);
                    if (h) rightClicked_.insert(h);
                }
                break;
            }
            mouseDown_ = false;
            if (active_ && hitTest(px_, py_, true) == active_) clicked_.insert(active_);
            active_ = 0;
            if (e.type == InputEvent::TouchUp) px_ = py_ = -1;
            break;
        }
        case InputEvent::Wheel: {
            for (Id s : scrollersAt(px_, py_)) {
                const LastNode& n = last_[s];
                float& v = scroll_[s];
                const float before = v;
                v = std::clamp(v - e.dy * 80.0f, 0.0f, n.maxScroll);
                if (v != before) break;
            }
            break;
        }
        case InputEvent::Text:
        case InputEvent::Paste:
        case InputEvent::Cut:
        case InputEvent::Copy:
            if (focusIsText_) textEvents_.push_back(e);
            break;
        case InputEvent::KeyDown: {
            usingPad_ = false;
            const bool ctrl = (e.mods & (ModCtrl | ModMeta)) != 0;
            if (focusIsText_) {
                const bool edit = e.key == Key::Left || e.key == Key::Right || e.key == Key::Home || e.key == Key::End ||
                                  e.key == Key::Backspace || e.key == Key::Delete || e.key == Key::Enter ||
                                  (ctrl && (e.key == Key::A || e.key == Key::C || e.key == Key::X || e.key == Key::V));
                if (e.key == Key::Escape) {
                    textEvents_.push_back(e);
                    backPressed_ = true;
                    break;
                }
                if (edit) {
                    textEvents_.push_back(e);
                    break;
                }
                if (e.key == Key::Tab) {
                    focusVisible_ = true;
                    moveFocusSeq((e.mods & ModShift) ? -1 : 1);
                    break;
                }
                if (e.key == Key::Up || e.key == Key::Down) {
                    focusVisible_ = true;
                    moveFocus(0, e.key == Key::Up ? -1 : 1);
                    break;
                }
                break;  // printable keys arrive as Text
            }
            if (sceneKeys_) {
                // The keys drive: nothing in the UI moves or is pressed by them.
                if (e.key == Key::Escape) backPressed_ = true;
                else keysFree_.push_back({e.key, e.mods});
                break;
            }
            switch (e.key) {
                case Key::Escape: backPressed_ = true; break;
                case Key::Enter:
                case Key::Space:
                    if (focus_ && !e.repeat) {
                        clicked_.insert(focus_);
                        focusVisible_ = true;
                    } else {
                        keysFree_.push_back({e.key, e.mods});
                    }
                    break;
                case Key::Tab:
                    focusVisible_ = true;
                    moveFocusSeq((e.mods & ModShift) ? -1 : 1);
                    break;
                case Key::Left:
                case Key::Right:
                    if (focus_ && (focusFlags_ & InteractArrowsLR)) {
                        keysFree_.push_back({e.key, e.mods});
                        focusVisible_ = true;
                    } else if (focus_ || !focusables_.empty()) {
                        focusVisible_ = true;
                        moveFocus(e.key == Key::Left ? -1 : 1, 0);
                    } else {
                        keysFree_.push_back({e.key, e.mods});
                    }
                    break;
                case Key::Up:
                case Key::Down:
                    if (focus_ && (focusFlags_ & InteractArrowsUD)) {
                        keysFree_.push_back({e.key, e.mods});
                        focusVisible_ = true;
                    } else if (focus_ || !focusables_.empty()) {
                        focusVisible_ = true;
                        moveFocus(0, e.key == Key::Up ? -1 : 1);
                    } else {
                        keysFree_.push_back({e.key, e.mods});
                    }
                    break;
                default: keysFree_.push_back({e.key, e.mods}); break;
            }
            break;
        }
        default: break;
    }
}

bool Ui::keyPressed(Key k, uint8_t mods) const {
    for (const auto& p : keysFree_)
        if (p.first == k && (mods == 0 || (p.second & mods) == mods)) return true;
    return false;
}

void Ui::moveFocusSeq(int dir) {
    std::vector<const Focusable*> c;
    for (const Focusable& f : focusables_)
        if (lastModalLayer_ < 0 || f.layer >= lastModalLayer_) c.push_back(&f);
    if (c.empty()) return;
    int at = -1;
    for (size_t i = 0; i < c.size(); ++i)
        if (c[i]->id == focus_) at = int(i);
    int n = at < 0 ? (dir > 0 ? 0 : int(c.size()) - 1) : (at + dir + int(c.size())) % int(c.size());
    focus_ = c[size_t(n)]->id;
    focusFlags_ = c[size_t(n)]->flags;
    focusIsText_ = (focusFlags_ & InteractText) != 0;
    scrollTo_ = focus_;
}

void Ui::moveFocus(int dx, int dy) {
    std::vector<const Focusable*> c;
    int topLayer = lastModalLayer_;
    for (const Focusable& f : focusables_) topLayer = std::max(topLayer, f.layer);
    for (const Focusable& f : focusables_)
        if ((lastModalLayer_ < 0 || f.layer >= lastModalLayer_) && f.layer == topLayer) c.push_back(&f);
    if (c.empty()) return;
    const Focusable* cur = nullptr;
    for (const Focusable* f : c)
        if (f->id == focus_) cur = f;
    if (!cur) {
        focus_ = c[0]->id;
        focusFlags_ = c[0]->flags;
        focusIsText_ = (focusFlags_ & InteractText) != 0;
        scrollTo_ = focus_;
        return;
    }
    const Rect& a = cur->rect;
    const float acx = a.x + a.w * 0.5f, acy = a.y + a.h * 0.5f;
    const Focusable* best = nullptr;
    float bestScore = 1e30f;
    for (const Focusable* f : c) {
        if (f == cur) continue;
        const Rect& b = f->rect;
        const float bcx = b.x + b.w * 0.5f, bcy = b.y + b.h * 0.5f;
        float primary, perp;
        if (dy != 0) {
            if (dy > 0 ? bcy <= acy + 1.0f : bcy >= acy - 1.0f) continue;
            primary = dy > 0 ? b.y - a.bottom() : a.y - b.bottom();
            const float ov = std::min(a.right(), b.right()) - std::max(a.x, b.x);
            perp = ov > 0 ? 0.0f : -ov;
        } else {
            if (dx > 0 ? bcx <= acx + 1.0f : bcx >= acx - 1.0f) continue;
            primary = dx > 0 ? b.x - a.right() : a.x - b.right();
            const float ov = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
            perp = ov > 0 ? 0.0f : -ov;
        }
        primary = std::max(primary, -std::min(a.w, a.h) * 0.25f);
        const float score = primary + perp * 4.0f + (dy != 0 ? std::fabs(bcx - acx) : std::fabs(bcy - acy)) * 0.05f;
        if (score < bestScore) {
            bestScore = score;
            best = f;
        }
    }
    if (best) {
        focus_ = best->id;
        focusFlags_ = best->flags;
        focusIsText_ = (focusFlags_ & InteractText) != 0;
        scrollTo_ = focus_;
    }
}

Interact Ui::interact(Id id, unsigned flags) {
    Interact r;
    r.id = id;
    interactiveNow_[id] = flags;
    auto it = last_.find(id);
    if (it != last_.end()) r.rect = it->second.rect;
    const bool inert = modalLayer_ >= 0 ? int(layer_) < modalLayer_ : (lastModalLayer_ >= 0 && int(layer_) < lastModalLayer_);
    if (inert) return r;
    r.hover = hot_ == id && (!mouseDown_ || active_ == id);
    r.active = active_ == id && hot_ == id;
    r.focused = focus_ == id;
    r.focusVisible = r.focused && focusVisible_;
    r.clicked = clicked_.count(id) != 0;
    r.rightClicked = rightClicked_.count(id) != 0;
    r.doubleClicked = doubleClicked_.count(id) != 0;
    return r;
}

// ------------------------------------------------------------------------------------- text editing

unsigned Ui::editText(Id id, std::string& text, size_t maxChars, bool upper) {
    TextFieldState& st = textStates_[id];
    if (st.cursor > text.size()) st.cursor = text.size();
    if (st.anchor > text.size()) st.anchor = text.size();
    if (focus_ != id) {
        st.dragging = false;
        return 0;
    }
    unsigned out = 0;
    auto selMin = [&]() { return std::min(st.cursor, st.anchor); };
    auto selMax = [&]() { return std::max(st.cursor, st.anchor); };
    auto eraseSel = [&]() {
        if (st.cursor == st.anchor) return false;
        const size_t a = selMin(), b = selMax();
        text.erase(a, b - a);
        st.cursor = st.anchor = a;
        out |= EditChanged;
        return true;
    };
    auto insert = [&](std::string s) {
        std::string clean;
        for (size_t i = 0; i < s.size();) {
            const uint32_t cp = decodeUtf8(s, i);
            if (cp < 32 || cp == 127) continue;
            appendUtf8(clean, cp);
        }
        if (upper) clean = toUpperUtf8(clean);
        eraseSel();
        // Respect the length limit in characters.
        size_t room = maxChars > 0 ? (maxChars > utf8Length(text) ? maxChars - utf8Length(text) : 0) : clean.size();
        std::string take;
        for (size_t i = 0; i < clean.size() && room > 0; --room) {
            const size_t a = i;
            decodeUtf8(clean, i);
            take += clean.substr(a, i - a);
        }
        if (take.empty()) return;
        text.insert(st.cursor, take);
        st.cursor += take.size();
        st.anchor = st.cursor;
        out |= EditChanged;
    };
    auto wordLeft = [&](size_t i) {
        while (i > 0) {
            const size_t p = utf8Prev(text, i);
            size_t q = p;
            if (isWordChar(decodeUtf8(text, q))) break;
            i = p;
        }
        while (i > 0) {
            const size_t p = utf8Prev(text, i);
            size_t q = p;
            if (!isWordChar(decodeUtf8(text, q))) break;
            i = p;
        }
        return i;
    };
    auto wordRight = [&](size_t i) {
        while (i < text.size()) {
            size_t q = i;
            if (isWordChar(decodeUtf8(text, q))) break;
            i = q;
        }
        while (i < text.size()) {
            size_t q = i;
            if (!isWordChar(decodeUtf8(text, q))) break;
            i = q;
        }
        return i;
    };
    auto byteAt = [&](float x) {
        // The nearest caret stop to x (px, field-relative) from the last frame's layout, as a
        // character count, then the byte offset of that character in the text.
        uint32_t bestK = 0;
        float bestD = 1e30f;
        for (const auto& s : caretStops_[id]) {
            const float d = std::fabs(s.second - x);
            if (d < bestD) {
                bestD = d;
                bestK = s.first;
            }
        }
        size_t i = 0;
        for (uint32_t k = 0; k < bestK && i < text.size(); ++k) i = utf8Next(text, i);
        return i;
    };
    for (const InputEvent& e : textEvents_) {
        const bool ctrl = (e.mods & (ModCtrl | ModMeta)) != 0;
        const bool shift = (e.mods & ModShift) != 0;
        switch (e.type) {
            case InputEvent::Text: insert(e.text); break;
            case InputEvent::Paste: insert(e.text); break;
            case InputEvent::Copy:
                if (st.cursor != st.anchor) host_->setClipboard(text.substr(selMin(), selMax() - selMin()));
                break;
            case InputEvent::Cut:
                if (st.cursor != st.anchor) {
                    host_->setClipboard(text.substr(selMin(), selMax() - selMin()));
                    eraseSel();
                }
                break;
            case InputEvent::MouseDown:
            case InputEvent::TouchDown: {
                const size_t b = byteAt(e.x / scale_ - caretX0_[id]);
                if (doubleClicked_.count(id)) {
                    st.anchor = wordLeft(b);
                    st.cursor = wordRight(b);
                } else {
                    st.cursor = b;
                    if (!shift) st.anchor = b;
                    st.dragging = true;
                }
                break;
            }
            case InputEvent::KeyDown:
                switch (e.key) {
                    case Key::Left: {
                        if (st.cursor != st.anchor && !shift) st.cursor = selMin();
                        else st.cursor = ctrl ? wordLeft(st.cursor) : utf8Prev(text, st.cursor);
                        if (!shift) st.anchor = st.cursor;
                        break;
                    }
                    case Key::Right: {
                        if (st.cursor != st.anchor && !shift) st.cursor = selMax();
                        else st.cursor = ctrl ? wordRight(st.cursor) : utf8Next(text, st.cursor);
                        if (!shift) st.anchor = st.cursor;
                        break;
                    }
                    case Key::Home:
                        st.cursor = 0;
                        if (!shift) st.anchor = 0;
                        break;
                    case Key::End:
                        st.cursor = text.size();
                        if (!shift) st.anchor = st.cursor;
                        break;
                    case Key::Backspace:
                        if (!eraseSel() && st.cursor > 0) {
                            const size_t a = ctrl ? wordLeft(st.cursor) : utf8Prev(text, st.cursor);
                            text.erase(a, st.cursor - a);
                            st.cursor = st.anchor = a;
                            out |= EditChanged;
                        }
                        break;
                    case Key::Delete:
                        if (!eraseSel() && st.cursor < text.size()) {
                            const size_t b = ctrl ? wordRight(st.cursor) : utf8Next(text, st.cursor);
                            text.erase(st.cursor, b - st.cursor);
                            st.anchor = st.cursor;
                            out |= EditChanged;
                        }
                        break;
                    case Key::A:
                        if (ctrl) {
                            st.anchor = 0;
                            st.cursor = text.size();
                        }
                        break;
                    case Key::C:
                        if (ctrl && st.cursor != st.anchor) host_->setClipboard(text.substr(selMin(), selMax() - selMin()));
                        break;
                    case Key::V:
                        if (ctrl) {
                            // (In a browser the text comes as a Paste event instead.)
                            const std::string t = host_->clipboardText();
                            if (!t.empty()) insert(t);
                        }
                        break;
                    case Key::X:
                        if (ctrl && st.cursor != st.anchor) {
                            host_->setClipboard(text.substr(selMin(), selMax() - selMin()));
                            eraseSel();
                        }
                        break;
                    case Key::Enter: out |= EditSubmit; break;
                    case Key::Escape: out |= EditCancel; break;
                    default: break;
                }
                st.blinkStart = time_;
                break;
            default: break;
        }
    }
    // Dragging selects.
    if (st.dragging) {
        if (mouseDown_ && active_ == id) st.cursor = byteAt(px_ - caretX0_[id]);
        else st.dragging = false;
    }
    if (out & EditChanged) st.blinkStart = time_;
    return out;
}

// ------------------------------------------------------------------------------------- toasts

void Ui::toast(const std::string& text, float seconds) {
    toast_ = text;
    toastUntil_ = time_ + seconds;
}

std::vector<Ui::Snapshot> Ui::snapshot() const {
    std::vector<Snapshot> out;
    for (const Node& n : nodes_) {
        if (n.label.empty() && !n.s.focusable) continue;
        Rect vis = n.rect;
        auto it = last_.find(n.id);
        if (it != last_.end()) vis = n.rect.intersect(it->second.clip);
        out.push_back(Snapshot{n.id, n.label, n.rect, vis, n.layer, n.s.focusable});
    }
    return out;
}

}  // namespace ui
}  // namespace cc
