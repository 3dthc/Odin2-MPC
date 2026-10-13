/*
** odin2-mpc: the parts of OdinAudioProcessor that JUCE's AudioProcessor used to provide
** (sample rate setup, state save/restore, patch loading), rewritten without JUCE.
** Included from PluginProcessor.cpp like Odin's other split files. GPL-3.0-or-later, as Odin 2.
*/

//this file is included from PluginProcessor.cpp to split the class implementation

#include "PluginProcessor.h"

void OdinAudioProcessor::prepare(float p_samplerate) {
	setSampleRate(p_samplerate);
}

// Same steps as Odin's patch browser: refuse newer patches, load the init patch first under an older one
// (readPatch does that itself), read it, rebuild the drawn wavetables, restamp the version.
bool OdinAudioProcessor::loadPatchFromMemory(const void *p_data, size_t p_size) {
	MemoryInputStream stream(p_data, p_size, false);
	ValueTree patch = ValueTree::readFromStream(stream);
	if (!patch.isValid() || !(patch.getType() == m_value_tree.state.getType())) {
		return false;
	}
	ValueTree misc = patch.getChildWithName("misc");
	if (!misc.isValid() || (int)misc["patch_migration_version"] > ODIN_PATCH_MIGRATION_VERSION) {
		return false;
	}

	readPatch(patch);
	createDrawTablesFromValueTree();
	applyFXOrderFromTree();
	resetAudioEngine();

	m_value_tree.state.getChildWithName("misc").setProperty("version_minor", ODIN_MINOR_VERSION, nullptr);
	m_value_tree.state.getChildWithName("misc").setProperty("version_patch", ODIN_PATCH_VERSION, nullptr);
	m_value_tree.state.getChildWithName("misc").setProperty("patch_migration_version", ODIN_PATCH_MIGRATION_VERSION, nullptr);
	return true;
}

// The state as JUCE's AudioProcessorValueTreeState stores it: the "Odin" tree with its non-parameter children,
// plus one PARAM child (id, value) per parameter. This is also the .odin preset file format.
std::string OdinAudioProcessor::savePatchToMemory() {
	ValueTree out = m_value_tree.state.createCopy();
	for (auto &p : m_value_tree.all()) {
		ValueTree param("PARAM");
		param.setProperty("id", p->paramID, nullptr);
		param.setProperty("value", (double)p->value.load(), nullptr);
		out.appendChild(param, nullptr);
	}
	MemoryOutputStream stream;
	out.writeToStream(stream);
	return stream.data;
}

// The FX order lives in the "fx" tree, but upstream only the editor passed it to the audio engine.
void OdinAudioProcessor::applyFXOrderFromTree() {
	ValueTree fx = m_value_tree.state.getChildWithName("fx");
	setFXButtonsPosition((int)fx["delay_position"],
	                     (int)fx["phaser_position"],
	                     (int)fx["flanger_position"],
	                     (int)fx["chorus_position"],
	                     (int)fx["reverb_position"]);
}

// STOP: silence everything now: voices, envelopes, arpeggiator and every effect tail.
void OdinAudioProcessor::panic() {
	m_arpeggiator.endPlayingNotes();
	m_playmode_mono_note_list.clear();
	resetAudioEngine();
	m_reverb_zita.reset();
	m_last_midi_note = -1;
}
