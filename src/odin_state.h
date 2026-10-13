/* odin2-mpc: Odin's whole patch as text, and back.
 *
 * A snapshot is every Odin parameter ("<id>") and every patch property of its value tree ("<tree>.<property>"),
 * one "key=value" line each, in the order Odin's own patch loader applies them (drawn tables, oscillators,
 * effects, LFOs, misc, mod matrix, then parameters). The factory bank and the project state are both stored as
 * the lines that differ from a base snapshot, which keeps a project's state small (the MPC wrapper's chunk is 8 KB).
 *
 * GPL-3.0-or-later, as Odin 2. */
#pragma once
#include "PluginProcessor.h"
#include <map>
#include <string>
#include <vector>

class OdinMPC {
public:
    typedef std::vector<std::pair<std::string, std::string>> Lines;

    /* the order trees are applied in (Odin's readPatch), then the parameters */
    static const char *const *treeOrder() {
        static const char *const t[] = {"draw", "osc", "fx", "lfo", "misc", "mod", nullptr};
        return t;
    }

    /* keys that are not part of the sound: host tempo, GUI selections, file names, versions, wheels */
    static bool ignored(const std::string &k) {
        static const char *const skip[] = {"misc.BPM", "misc.env_left_selected", "misc.env_right_selected",
                                           "misc.current_patch_filename", "misc.current_patch_directory",
                                           "misc.arp_mod_selected", "misc.preset_soundbank_selected",
                                           "misc.preset_category_selected", "misc.preset_patch_selected",
                                           "misc.version_minor", "misc.version_patch", "misc.patch_migration_version",
                                           "fx.delay_selected", "fx.phaser_selected", "fx.flanger_selected",
                                           "fx.chorus_selected", "fx.reverb_selected", "lfo.lfo_left_selected",
                                           "lfo.lfo_right_selected", "modwheel", "pitchbend", "misc.patch_name",
                                           nullptr};
        for (int i = 0; skip[i]; i++)
            if (k == skip[i]) return true;
        return false;
    }

    static std::string num(double v) {
        char b[40];
        snprintf(b, sizeof b, "%.7g", v);
        return b;
    }

    static Lines snapshot(OdinAudioProcessor &p) {
        Lines out;
        ValueTree st = p.m_value_tree.state;
        for (const char *const *t = treeOrder(); *t; ++t) {
            ValueTree c = st.getChildWithName(*t);
            for (int i = 0; i < c.getNumProperties(); i++) {
                Identifier name = c.getPropertyName(i);
                std::string k = std::string(*t) + "." + name.toString().toStdString();
                if (ignored(k)) continue;
                var v = c.getPropertyAt(i);
                out.emplace_back(k, v.isString() ? std::string("\"") + v.toString().toStdString() : num((double)v));
            }
        }
        for (auto &prm : p.m_value_tree.all()) {
            std::string k = prm->paramID.toStdString();
            if (ignored(k)) continue;
            out.emplace_back(k, num(prm->value.load()));
        }
        return out;
    }

    /* lines of b that differ from a (same keys, same order: both are snapshots of one engine) */
    static Lines diff(const Lines &a, const Lines &b) {
        if (a.size() == b.size()) {   // the usual case: two snapshots, same keys in the same order
            Lines out;
            size_t i = 0;
            for (; i < a.size() && a[i].first == b[i].first; i++)
                if (a[i].second != b[i].second) out.push_back(b[i]);
            if (i == a.size()) return out;
        }
        std::map<std::string, std::string> base(a.begin(), a.end());
        Lines out;
        for (auto &kv : b) {
            auto it = base.find(kv.first);
            if (it == base.end() || it->second != kv.second) out.push_back(kv);
        }
        return out;
    }

    /* base with the lines of top replacing (or adding to) its own, keeping base's order */
    static Lines overlay(const Lines &base, const Lines &top) {
        std::map<std::string, std::string> t(top.begin(), top.end());
        Lines out;
        for (auto &kv : base) {
            auto it = t.find(kv.first);
            out.emplace_back(kv.first, it == t.end() ? kv.second : it->second);
            if (it != t.end()) t.erase(it);
        }
        for (auto &kv : top)
            if (t.count(kv.first)) out.push_back(kv);
        return out;
    }

    static std::string join(const Lines &l) {
        std::string s;
        for (auto &kv : l) s += kv.first + "=" + kv.second + "\n";
        return s;
    }

    static Lines parse(const char *text) {
        Lines out;
        const char *p = text;
        while (p && *p) {
            const char *e = strchr(p, '\n');
            std::string line = e ? std::string(p, e - p) : std::string(p);
            size_t eq = line.find('=');
            if (eq != std::string::npos && eq > 0) out.emplace_back(line.substr(0, eq), line.substr(eq + 1));
            p = e ? e + 1 : nullptr;
        }
        return out;
    }

    /* Apply lines in Odin's patch-loading order, then the follow-ups its patch browser and editor did. */
    static void apply(OdinAudioProcessor &p, const Lines &lines) {
        ValueTree st = p.m_value_tree.state;
        auto treeOf = [](const std::string &k) { return k.substr(0, k.find('.')); };
        for (const char *const *t = treeOrder(); *t; ++t) {
            ValueTree c = st.getChildWithName(*t);
            for (auto &kv : lines) {
                if (kv.first.find('.') == std::string::npos || treeOf(kv.first) != *t || ignored(kv.first)) continue;
                std::string prop = kv.first.substr(kv.first.find('.') + 1);
                if (!kv.second.empty() && kv.second[0] == '"') c.setProperty(Identifier(prop), var(kv.second.substr(1)), nullptr);
                else c.setProperty(Identifier(prop), var(atof(kv.second.c_str())), nullptr);
            }
        }
        for (auto &kv : lines) {
            if (kv.first.find('.') != std::string::npos || ignored(kv.first)) continue;
            p.m_value_tree.setValue(p.m_value_tree.getParameter(kv.first), (float)atof(kv.second.c_str()));
        }
        p.createDrawTablesFromValueTree();
        p.applyFXOrderFromTree();
        p.setMonoPolyLegato(VALUETREETOPLAYMODE((int)st.getChildWithName("misc")["legato"]));
    }
};
