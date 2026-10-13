/* JUCE stand-in for the MPC OS port of Odin 2 (odin2-mpc).
 *
 * Odin's audio engine and processor glue were written against JUCE. The MPC plugin host is a plain VST2
 * loader and the port is built by mpc-vst-plugins' C wrapper, so instead of JUCE this header provides just
 * the pieces Odin's processor code touches: String/Identifier/var, a ValueTree with JUCE's binary stream
 * format (the .odin preset format), an AudioProcessorValueTreeState that holds the parameters, and a few
 * DSP helpers (Decibels, FastMathApproximations). Behaviour follows JUCE where Odin depends on it:
 * listeners fire only when a value actually changes, ranges convert with JUCE's skew formula.
 *
 * Copyright (C) 2026 odin2-mpc contributors. GPL-3.0-or-later, like Odin 2 itself. */
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <unordered_map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DBG(x) do { } while (0)
#define jassert(x) do { } while (0)
#define jassertfalse do { } while (0)
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(x)
#define JUCE_DEBUG 0
#define JUCE_DISABLE_ASSERTIONS 1
#define MIDI_CONTROLLER_NUMBER_BREATH 2

namespace juce {

/* ---------------------------------------------------------------- strings */
class String : public std::string {
public:
    String() {}
    String(const char *s) : std::string(s ? s : "") {}
    String(const std::string &s) : std::string(s) {}
    String(std::string &&s) : std::string(std::move(s)) {}
    explicit String(int v) : std::string(std::to_string(v)) {}
    explicit String(float v) : std::string(fmt(v)) {}
    explicit String(double v) : std::string(fmt(v)) {}
    const std::string &toStdString() const { return *this; }
    String toString() const { return *this; }
    int getIntValue() const { return atoi(c_str()); }
    float getFloatValue() const { return (float)atof(c_str()); }
    double getDoubleValue() const { return atof(c_str()); }
    bool isEmpty() const { return empty(); }
    bool isNotEmpty() const { return !empty(); }
    int length() const { return (int)size(); }
    bool startsWith(const std::string &p) const { return compare(0, p.size(), p) == 0; }
    bool contains(const std::string &p) const { return find(p) != npos; }
    static std::string fmt(double v) {
        char b[48];
        snprintf(b, sizeof b, "%.9g", v);
        return b;
    }
};
inline String operator+(const String &a, const char *b) { return String(static_cast<const std::string &>(a) + b); }
inline String operator+(const char *a, const String &b) { return String(a + static_cast<const std::string &>(b)); }
inline String operator+(const String &a, const std::string &b) { return String(static_cast<const std::string &>(a) + b); }
inline String operator+(const std::string &a, const String &b) { return String(a + static_cast<const std::string &>(b)); }
inline String operator+(const String &a, const String &b) { return String(static_cast<const std::string &>(a) + static_cast<const std::string &>(b)); }

class Identifier {
public:
    Identifier() {}
    Identifier(const char *s) : m(s ? s : "") {}
    Identifier(const std::string &s) : m(s) {}
    Identifier(const String &s) : m(s) {}
    const String &toString() const { return m; }
    operator const String &() const { return m; }
    bool operator==(const Identifier &o) const { return str() == o.str(); }
    bool operator!=(const Identifier &o) const { return str() != o.str(); }
    bool operator<(const Identifier &o) const { return str() < o.str(); }
    const std::string &str() const { return m; }
    bool isValid() const { return !m.empty(); }
private:
    String m;
};

class StringRef {
public:
    StringRef(const char *s) : m(s ? s : "") {}
    StringRef(const std::string &s) : m(s) {}
    StringRef(const String &s) : m(s) {}
    bool operator==(const Identifier &id) const { return m == id.str(); }
    bool operator!=(const Identifier &id) const { return m != id.str(); }
    bool operator==(const char *s) const { return m == s; }
    operator const std::string &() const { return m; }
private:
    std::string m;
};
inline bool operator==(const Identifier &id, const StringRef &s) { return s == id; }

/* ---------------------------------------------------------------- var */
class var {
public:
    enum Type { Void, Int, Bool, Double, Str, Int64 };
    var() {}
    var(int v) : t(Int), d(v) {}
    var(int64_t v) : t(Int64), d((double)v) {}
    var(bool v) : t(Bool), d(v ? 1 : 0) {}
    var(float v) : t(Double), d(v) {}
    var(double v) : t(Double), d(v) {}
    var(const char *s) : t(Str), s(s ? s : "") {}
    var(const std::string &v) : t(Str), s(v) {}
    var(const String &v) : t(Str), s(v) {}
    Type type() const { return t; }
    bool isVoid() const { return t == Void; }
    bool isString() const { return t == Str; }
    double num() const { return t == Str ? atof(s.c_str()) : d; }
    operator int() const { return t == Str ? atoi(s.c_str()) : (int)d; }
    operator float() const { return (float)num(); }
    operator double() const { return num(); }
    operator bool() const { return t == Str ? !s.empty() : d != 0; }
    operator String() const { return toString(); }
    String toString() const {
        switch (t) {
        case Str: return s;
        case Void: return String();
        case Int: case Int64: case Bool: return String(std::to_string((long long)d));
        default: return String(String::fmt(d));
        }
    }
    bool equals(const var &o) const {
        if (t == Void || o.t == Void) return t == o.t;
        if (t == Str || o.t == Str) return toString().toStdString() == o.toString().toStdString();
        return d == o.d;
    }
private:
    Type t = Void;
    double d = 0;
    String s;
};

/* ---------------------------------------------------------------- binary streams (JUCE's on-disk format) */
class MemoryInputStream {
public:
    MemoryInputStream(const void *data, size_t size, bool /*copy*/) : p((const uint8_t *)data), n(size) {}
    bool ok() const { return !bad; }
    bool exhausted() const { return pos >= n; }
    uint8_t byte() { if (pos >= n) { bad = true; return 0; } return p[pos++]; }
    int compressedInt() {
        uint8_t h = byte();
        int nb = h & 0x7f;
        if (nb > 4) { bad = true; return 0; }
        unsigned v = 0;
        for (int i = 0; i < nb; i++) v |= (unsigned)byte() << (8 * i);
        return (h & 0x80) ? -(int)v : (int)v;
    }
    String str() {
        std::string r;
        while (true) { uint8_t c = byte(); if (bad || c == 0) break; r += (char)c; }
        return r;
    }
    void skip(int k) { if (k < 0 || pos + (size_t)k > n) { bad = true; pos = n; } else pos += k; }
    int32_t i32() { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)byte() << (8 * i); return (int32_t)v; }
    int64_t i64() { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)byte() << (8 * i); return (int64_t)v; }
    double f64() { int64_t v = i64(); double d; memcpy(&d, &v, 8); return d; }
private:
    const uint8_t *p;
    size_t n, pos = 0;
    bool bad = false;
};

class MemoryOutputStream {
public:
    std::string data;
    void byte(uint8_t b) { data += (char)b; }
    void compressedInt(int v) {
        unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
        uint8_t b[5]; int nb = 0;
        while (u) { b[++nb] = (uint8_t)(u & 0xff); u >>= 8; }
        b[0] = (uint8_t)nb | (v < 0 ? 0x80 : 0);
        for (int i = 0; i <= nb; i++) byte(b[i]);
    }
    void str(const std::string &s) { data += s; byte(0); }
    void i32(int32_t v) { for (int i = 0; i < 4; i++) byte((uint8_t)((uint32_t)v >> (8 * i))); }
    void f64(double d) { uint64_t v; memcpy(&v, &d, 8); for (int i = 0; i < 8; i++) byte((uint8_t)(v >> (8 * i))); }
};

inline var readVar(MemoryInputStream &in) {
    int size = in.compressedInt();
    if (size <= 0) return var();
    uint8_t marker = in.byte();
    switch (marker) {
    case 1: { int32_t v = in.i32(); in.skip(size - 5); return var((int)v); }
    case 2: in.skip(size - 1); return var(true);
    case 3: in.skip(size - 1); return var(false);
    case 4: { double v = in.f64(); in.skip(size - 9); return var(v); }
    case 5: { String s = in.str(); in.skip(size - 2 - (int)s.size()); return var(s); }
    case 6: { int64_t v = in.i64(); in.skip(size - 9); return var(v); }
    default: in.skip(size - 1); return var();   /* arrays, binary blobs, undefined: Odin stores none */
    }
}

inline void writeVar(MemoryOutputStream &o, const var &v) {
    switch (v.type()) {
    case var::Void: o.compressedInt(0); break;
    case var::Int: o.compressedInt(5); o.byte(1); o.i32((int)v); break;
    case var::Bool: o.compressedInt(1); o.byte((bool)v ? 2 : 3); break;
    case var::Double: case var::Int64: o.compressedInt(9); o.byte(4); o.f64((double)v); break;
    case var::Str: { std::string s = v.toString(); o.compressedInt((int)s.size() + 2); o.byte(5); o.str(s); break; }
    }
}

/* ---------------------------------------------------------------- ValueTree (shared, like JUCE's) */
class UndoManager;

class ValueTree {
public:
    class Listener {
    public:
        virtual ~Listener() {}
        virtual void valueTreePropertyChanged(ValueTree &, const Identifier &) {}
        virtual void valueTreeChildAdded(ValueTree &, ValueTree &) {}
        virtual void valueTreeChildRemoved(ValueTree &, ValueTree &, int) {}
        virtual void valueTreeChildOrderChanged(ValueTree &, int, int) {}
        virtual void valueTreeParentChanged(ValueTree &) {}
    };

    ValueTree() {}
    explicit ValueTree(const Identifier &type) : n(std::make_shared<Node>()) { n->type = type; }
    ValueTree(const char *type) : ValueTree(Identifier(type)) {}

    bool isValid() const { return (bool)n; }
    Identifier getType() const { return n ? n->type : Identifier(); }
    bool hasType(const Identifier &t) const { return n && n->type == t; }

    int getNumProperties() const { return n ? (int)n->props.size() : 0; }
    Identifier getPropertyName(int i) const { return n && i >= 0 && i < (int)n->props.size() ? n->props[i].first : Identifier(); }
    bool hasProperty(const Identifier &name) const { return find(name) >= 0; }
    var getProperty(const Identifier &name) const { int i = find(name); return i >= 0 ? n->props[i].second : var(); }
    var getProperty(const Identifier &name, const var &def) const { int i = find(name); return i >= 0 ? n->props[i].second : def; }
    var operator[](const Identifier &name) const { return getProperty(name); }
    var operator[](const char *name) const { return getProperty(Identifier(name)); }

    ValueTree &setProperty(const Identifier &name, const var &v, UndoManager *) {
        if (!n) return *this;
        int i = find(name);
        if (i >= 0) {
            if (n->props[i].second.equals(v) && n->props[i].second.type() == v.type()) return *this;
            n->props[i].second = v;
        } else {
            n->index[name.toString()] = n->props.size();
            n->props.emplace_back(name, v);
        }
        sendPropertyChangeMessage(name);
        return *this;
    }
    void removeProperty(const Identifier &name, UndoManager *) {
        int i = find(name);
        if (i >= 0) {
            n->props.erase(n->props.begin() + i);
            n->index.clear();
            for (size_t k = 0; k < n->props.size(); k++) n->index[n->props[k].first.toString()] = k;
        }
    }
    /* the i-th property's value (no lookup by name) */
    var getPropertyAt(int i) const { return n && i >= 0 && i < (int)n->props.size() ? n->props[i].second : var(); }
    void sendPropertyChangeMessage(const Identifier &name) {
        if (!n) return;
        auto ls = n->listeners;
        for (auto *l : ls) l->valueTreePropertyChanged(*this, name);
    }

    int getNumChildren() const { return n ? (int)n->children.size() : 0; }
    ValueTree getChild(int i) const { return n && i >= 0 && i < (int)n->children.size() ? n->children[i] : ValueTree(); }
    ValueTree getChildWithName(const Identifier &t) const {
        if (n) for (auto &c : n->children) if (c.getType() == t) return c;
        return ValueTree();
    }
    void appendChild(const ValueTree &c, UndoManager *) { if (n && c.n) n->children.push_back(c); }
    void addChild(const ValueTree &c, int idx, UndoManager *) {
        if (!n || !c.n) return;
        if (idx < 0 || idx > (int)n->children.size()) n->children.push_back(c);
        else n->children.insert(n->children.begin() + idx, c);
    }
    void removeChild(int i, UndoManager *) { if (n && i >= 0 && i < (int)n->children.size()) n->children.erase(n->children.begin() + i); }

    void addListener(Listener *l) { if (n) n->listeners.push_back(l); }
    void removeListener(Listener *l) {
        if (n) n->listeners.erase(std::remove(n->listeners.begin(), n->listeners.end(), l), n->listeners.end());
    }

    ValueTree createCopy() const {
        if (!n) return ValueTree();
        ValueTree c(n->type);
        c.n->props = n->props;
        c.n->index = n->index;
        for (auto &ch : n->children) c.n->children.push_back(ch.createCopy());
        return c;
    }

    bool operator==(const ValueTree &o) const { return n == o.n; }
    bool operator!=(const ValueTree &o) const { return n != o.n; }

    /* JUCE's binary format: type, properties (name, var), children */
    static ValueTree readFromStream(MemoryInputStream &in) {
        String type = in.str();
        if (!in.ok() || type.empty()) return ValueTree();
        ValueTree t{Identifier(type)};
        int np = in.compressedInt();
        if (np < 0 || np > 100000) return ValueTree();
        for (int i = 0; i < np && in.ok(); i++) {
            String name = in.str();
            var v = readVar(in);
            if (!name.empty() && !t.n->index.count(name)) {
                t.n->index[name] = t.n->props.size();
                t.n->props.emplace_back(Identifier(name), v);
            }
        }
        int nc = in.compressedInt();
        if (nc < 0 || nc > 100000) return ValueTree();
        for (int i = 0; i < nc && in.ok(); i++) {
            ValueTree c = readFromStream(in);
            if (!c.isValid()) return ValueTree();
            t.n->children.push_back(c);
        }
        return in.ok() ? t : ValueTree();
    }
    void writeToStream(MemoryOutputStream &o) const {
        if (!n) { o.str(""); return; }
        o.str(n->type.toString());
        o.compressedInt((int)n->props.size());
        for (auto &p : n->props) { o.str(p.first.toString()); writeVar(o, p.second); }
        o.compressedInt((int)n->children.size());
        for (auto &c : n->children) c.writeToStream(o);
    }

private:
    struct Node {
        Identifier type;
        std::vector<std::pair<Identifier, var>> props;
        std::unordered_map<std::string, size_t> index;   // property name -> position in props
        std::vector<ValueTree> children;
        std::vector<Listener *> listeners;
    };
    std::shared_ptr<Node> n;
    int find(const Identifier &name) const {
        if (!n) return -1;
        auto it = n->index.find(name.toString());
        return it == n->index.end() ? -1 : (int)it->second;
    }
};

/* ---------------------------------------------------------------- parameters */
template <typename T>
struct NormalisableRange {
    T start = 0, end = 1, interval = 0, skew = 1;
    NormalisableRange() {}
    NormalisableRange(T s, T e) : start(s), end(e) {}
    NormalisableRange(T s, T e, T i) : start(s), end(e), interval(i) {}
    NormalisableRange(T s, T e, T i, T k) : start(s), end(e), interval(i), skew(k) {}
    T convertFrom0to1(T p) const {
        p = p < 0 ? 0 : p > 1 ? 1 : p;
        if (skew != 1 && p > 0) p = std::exp(std::log(p) / skew);
        return snap(start + (end - start) * p);
    }
    T convertTo0to1(T v) const {
        T p = (end > start) ? (v - start) / (end - start) : 0;
        p = p < 0 ? 0 : p > 1 ? 1 : p;
        if (skew != 1 && p > 0) p = std::pow(p, skew);
        return p;
    }
    T snap(T v) const {
        if (interval > 0) v = start + interval * std::floor((v - start) / interval + (T)0.5);
        return v < start ? start : v > end ? end : v;
    }
};

class AudioProcessorValueTreeState;

class RangedAudioParameter {
public:
    RangedAudioParameter(const String &id, const String &name, NormalisableRange<float> r, float def, bool isInt)
        : paramID(id), name(name), range(r), def(def), is_int(isInt) { value.store(def); }
    virtual ~RangedAudioParameter() {}
    float convertTo0to1(float v) const { return range.convertTo0to1(v); }
    float convertFrom0to1(float p) const {
        float v = range.convertFrom0to1(p);
        return is_int ? std::round(v) : v;
    }
    void setValueNotifyingHost(float norm);
    float getValue() const { return convertTo0to1(value.load()); }
    void beginChangeGesture() {}
    void endChangeGesture() {}

    String paramID, name;
    NormalisableRange<float> range;
    float def;
    bool is_int;
    std::atomic<float> value;
    AudioProcessorValueTreeState *owner = nullptr;
};

class AudioParameterFloat : public RangedAudioParameter {
public:
    AudioParameterFloat(const String &id, const String &name, NormalisableRange<float> r, float def)
        : RangedAudioParameter(id, name, r, def, false) {}
    AudioParameterFloat(const String &id, const String &name, float lo, float hi, float def)
        : RangedAudioParameter(id, name, NormalisableRange<float>(lo, hi), def, false) {}
};

class AudioParameterInt : public RangedAudioParameter {
public:
    AudioParameterInt(const String &id, const String &name, int lo, int hi, int def)
        : RangedAudioParameter(id, name, NormalisableRange<float>((float)lo, (float)hi, 1.f), (float)def, true) {}
};

class AudioProcessor {};

class AudioProcessorValueTreeState {
public:
    class Listener {
    public:
        virtual ~Listener() {}
        virtual void parameterChanged(const String &parameterID, float newValue) = 0;
    };
    struct ParameterLayout {
        std::vector<std::unique_ptr<RangedAudioParameter>> params;
        template <typename... Items>
        ParameterLayout(std::unique_ptr<Items>... items) {
            params.reserve(sizeof...(items));
            (params.emplace_back(std::move(items)), ...);   // a fold, not recursion: Odin passes ~260 parameters
        }
    };

    AudioProcessorValueTreeState(AudioProcessor &, UndoManager *, const Identifier &type, ParameterLayout layout)
        : state(type) {
        for (auto &p : layout.params) {
            p->owner = this;
            index[p->paramID] = (int)params.size();
            params.push_back(std::move(p));
        }
    }

    RangedAudioParameter *getParameter(const String &id) const {
        auto it = index.find(id);
        return it == index.end() ? nullptr : params[it->second].get();
    }
    std::atomic<float> *getRawParameterValue(const String &id) const {
        auto *p = getParameter(id);
        return p ? &p->value : nullptr;
    }
    void addParameterListener(const String &id, Listener *l) { listeners[id].push_back(l); }

    /* set a parameter in its own units; listeners fire only when the value changes (as in JUCE) */
    void setValue(RangedAudioParameter *p, float v) {
        if (!p) return;
        v = p->range.snap(v);
        if (p->is_int) v = std::round(v);
        if (p->value.load() == v) return;
        p->value.store(v);
        auto it = listeners.find(p->paramID);
        if (it != listeners.end()) for (auto *l : it->second) l->parameterChanged(p->paramID, v);
    }
    /* force the listeners even if unchanged (used once at start so every module matches its parameter) */
    void notify(RangedAudioParameter *p) {
        auto it = listeners.find(p->paramID);
        if (it != listeners.end()) for (auto *l : it->second) l->parameterChanged(p->paramID, p->value.load());
    }

    const std::vector<std::unique_ptr<RangedAudioParameter>> &all() const { return params; }

    ValueTree state;

private:
    std::vector<std::unique_ptr<RangedAudioParameter>> params;
    std::map<std::string, int> index;
    std::map<std::string, std::vector<Listener *>> listeners;
};

inline void RangedAudioParameter::setValueNotifyingHost(float norm) {
    if (owner) owner->setValue(this, convertFrom0to1(norm));
}

/* ---------------------------------------------------------------- DSP helpers */
struct Decibels {
    template <typename T>
    static T decibelsToGain(T db, T minusInf = T(-100)) { return db > minusInf ? std::pow(T(10), db * T(0.05)) : T(0); }
    template <typename T>
    static T gainToDecibels(T g, T minusInf = T(-100)) { return g > 0 ? std::max(minusInf, std::log10(g) * T(20)) : minusInf; }
};

struct ScopedNoDenormals {
    ScopedNoDenormals() {
#if defined(__arm__) && !defined(__aarch64__)
        asm volatile("vmrs %0, fpscr" : "=r"(saved));
        uint32_t v = saved | (1u << 24);   /* flush-to-zero */
        asm volatile("vmsr fpscr, %0" : : "r"(v));
#elif defined(__x86_64__) || defined(__i386__)
        asm volatile("stmxcsr %0" : "=m"(saved));
        uint32_t v = saved | 0x8040;       /* FTZ + DAZ */
        asm volatile("ldmxcsr %0" : : "m"(v));
#endif
    }
    ~ScopedNoDenormals() {
#if defined(__arm__) && !defined(__aarch64__)
        asm volatile("vmsr fpscr, %0" : : "r"(saved));
#elif defined(__x86_64__) || defined(__i386__)
        asm volatile("ldmxcsr %0" : : "m"(saved));
#endif
    }
    uint32_t saved = 0;
};

template <typename T>
struct Atomic {
    std::atomic<T> v;
    Atomic(T x = T()) : v(x) {}
    void set(T x) { v.store(x); }
    T get() const { return v.load(); }
};

namespace dsp {
/* JUCE's Padé approximations, as Odin uses them (same formulas as juce_FastMathApproximations.h) */
struct FastMathApproximations {
    template <typename T>
    static T exp(T x) {
        auto n = (T)1680 + x * ((T)840 + x * ((T)180 + x * ((T)20 + x)));
        auto d = (T)1680 + x * ((T)-840 + x * ((T)180 + x * ((T)-20 + x)));
        return n / d;
    }
    template <typename T>
    static T tanh(T x) {
        auto x2 = x * x;
        auto n = x * ((T)135135 + x2 * ((T)17325 + x2 * ((T)378 + x2)));
        auto d = (T)135135 + x2 * ((T)62370 + x2 * ((T)3150 + (T)28 * x2));
        return n / d;
    }
    template <typename T>
    static T tan(T x) {
        auto x2 = x * x;
        auto n = x * ((T)-135135 + x2 * ((T)17325 + x2 * ((T)-378 + x2)));
        auto d = (T)-135135 + x2 * ((T)62370 + x2 * ((T)-3150 + (T)28 * x2));
        return n / d;
    }
    template <typename T>
    static T cos(T x) {
        auto x2 = x * x;
        auto n = -((T)-39251520 + x2 * ((T)18471600 + x2 * ((T)-1075032 + (T)14615 * x2)));
        auto d = (T)39251520 + x2 * ((T)1154160 + x2 * ((T)16632 + x2 * (T)127));
        return n / d;
    }
    template <typename T>
    static T sin(T x) {
        auto x2 = x * x;
        auto n = -x * ((T)-11511339840 + x2 * ((T)1640635920 + x2 * ((T)-52785432 + x2 * (T)479249)));
        auto d = (T)11511339840 + x2 * ((T)277920720 + x2 * ((T)3177720 + x2 * (T)18361));
        return n / d;
    }
};
} // namespace dsp

/* ---------------------------------------------------------------- MIDI */
class MidiMessage {
public:
    MidiMessage() {}
    MidiMessage(const uint8_t *data, int len) {
        for (int i = 0; i < 3; i++) b[i] = i < len ? data[i] : 0;
        n = len;
    }
    int status() const { return b[0] & 0xf0; }
    bool isNoteOn() const { return status() == 0x90 && b[2] > 0; }
    bool isNoteOff() const { return status() == 0x80 || (status() == 0x90 && b[2] == 0); }
    int getNoteNumber() const { return b[1]; }
    int getVelocity() const { return b[2]; }
    bool isController() const { return status() == 0xb0; }
    int getControllerNumber() const { return b[1]; }
    int getControllerValue() const { return b[2]; }
    bool isAllNotesOff() const { return isController() && b[1] == 123; }
    bool isAllSoundOff() const { return isController() && b[1] == 120; }
    bool isPitchWheel() const { return status() == 0xe0; }
    int getPitchWheelValue() const { return b[1] | (b[2] << 7); }
    bool isSustainPedalOn() const { return isController() && b[1] == 64 && b[2] >= 64; }
    bool isSustainPedalOff() const { return isController() && b[1] == 64 && b[2] < 64; }
    bool isSoftPedalOn() const { return isController() && b[1] == 67 && b[2] >= 64; }
    bool isSoftPedalOff() const { return isController() && b[1] == 67 && b[2] < 64; }
    bool isAftertouch() const { return status() == 0xa0; }
    int getAfterTouchValue() const { return b[2]; }
    bool isChannelPressure() const { return status() == 0xd0; }
    int getChannelPressureValue() const { return b[1]; }
    bool isMidiClock() const { return b[0] == 0xf8; }
private:
    uint8_t b[3] = {0, 0, 0};
    int n = 0;
};

} // namespace juce

using namespace juce;
