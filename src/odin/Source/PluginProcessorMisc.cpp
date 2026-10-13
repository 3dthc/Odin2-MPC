/*
** Odin 2 Synthesizer Plugin
** Copyright (C) 2020 - 2021 TheWaveWarden
**
** Odin 2 is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** Odin 2 is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

//this file is included from PluginProcessor.cpp to split the class implementation

#include "PluginProcessor.h"

// odin2-mpc: the JUCE AudioProcessor methods (programs, editor, state) are replaced by PluginProcessorMPC.cpp

void OdinAudioProcessor::setSampleRate(float p_samplerate) {
	DBG("SampleRate was set to " + std::to_string((int)p_samplerate) + " Hz!");

	m_samplerate = p_samplerate;

	for (int voice = 0; voice < VOICES; ++voice) {
		m_voice[voice].setSampleRate(p_samplerate);
	}
	for (int stereo = 0; stereo < 2; ++stereo) {
		m_flanger[stereo].setSampleRate(p_samplerate);
		m_chorus[stereo].setSampleRate(p_samplerate);

		m_ladder_filter[stereo].setSampleRate(p_samplerate);
		m_korg_filter[stereo].setSampleRate(p_samplerate);
		m_diode_filter[stereo].setSampleRate(p_samplerate);
		m_comb_filter[stereo].setSampleRate(p_samplerate);
		m_formant_filter[stereo].setSampleRate(p_samplerate);
		m_SEM_filter_12[stereo].setSampleRate(p_samplerate);
		m_ring_mod[stereo].setSampleRate(p_samplerate);
	}

	m_delay.setSampleRate(p_samplerate);
	m_phaser.setSampleRate(p_samplerate);
	m_reverb_zita.setSampleRate(p_samplerate);
	m_reverb_zita.prepare();
	m_global_env.setSampleRate(p_samplerate);
	m_global_lfo.setSampleRate(p_samplerate);
	m_arpeggiator.setSampleRate(p_samplerate);

	//update glide
	float glide_samplerate_adjusted = pow((float)*m_value_tree.getRawParameterValue("glide"), 44800. / m_samplerate);
	for (int voice = 0; voice < VOICES; ++voice) {
		m_voice[voice].setGlide(glide_samplerate_adjusted);
	}
}

void OdinAudioProcessor::initializeModules() {
	m_global_env.reset();
	m_global_env.setEnvelopeOff(); // so it doesn't start by itself

	m_mod_matrix.setSourcesAndDestinations(&m_mod_sources, &m_mod_destinations);

	m_phaser.setRadiusBase(1.25f);

	// make "stereo" chorus & flanger
	m_chorus[1].setLFOResetPos(0.25);
	m_flanger[1].setLFOResetPos(0.5);

	setModulationPointers();

	// set analog osc as sync osc for start
	for (int voice = 0; voice < VOICES; ++voice) {
		//todo these are probably set wrongly on plugin load....
		m_voice[voice].setOscSyncOscillator(&(m_voice[voice].analog_osc[0]));
	}
}

void OdinAudioProcessor::setPitchWheelValue(int p_value) {
	*m_pitchbend = (float)(p_value - 8192) / 8192.f;
}

void OdinAudioProcessor::setModWheelValue(int p_value) {
	*m_modwheel = (float)(p_value) / 128.f;
}

void OdinAudioProcessor::checkEndGlobalEnvelope() {
	for (int voice = 0; voice < VOICES; ++voice) {
		if (m_voice[voice] && m_voice[voice].env[0].isBeforeRelease()) {
			// dont kill it
			return;
		}
	}
	// kill it
	m_global_env.startRelease();
	//DBG("kill global env");
}

void OdinAudioProcessor::resetAudioEngine() {

	for (int voice = 0; voice < VOICES; ++voice) {
		m_voice[voice].hardReset();
	}
	for (int stereo = 0; stereo < 2; ++stereo) {
		m_ladder_filter[stereo].reset();
		m_SEM_filter_12[stereo].reset();
		m_korg_filter[stereo].reset();
		m_diode_filter[stereo].reset();
		m_formant_filter[stereo].reset();
		m_comb_filter[stereo].reset();

		m_flanger[stereo].reset();
		m_chorus[stereo].reset();
	}

	m_delay.reset();
	m_phaser.reset();
	m_global_lfo.reset();
	m_global_env.reset();

	m_voice_manager.reset();
}

void OdinAudioProcessor::setBPM(float p_BPM) {
	for (int voice = 0; voice < VOICES; ++voice) {
		m_voice[voice].setBPM(p_BPM, m_lfo1_sync, m_lfo2_sync, m_lfo3_sync);
	}
	if (m_delay_sync) {
		m_delay.setFreqBPM(p_BPM);
	}
	if (m_phaser_sync) {
		m_phaser.setFreqBPM(p_BPM);
	}
	if (m_flanger_sync) {
		m_flanger[0].setFreqBPM(p_BPM);
		m_flanger[1].setFreqBPM(p_BPM);
	}
	if (m_chorus_sync) {
		m_chorus[0].setFreqBPM(p_BPM);
		m_chorus[1].setFreqBPM(p_BPM);
	}
	if (m_lfo4_sync) {
		m_global_lfo.setFreqBPM(p_BPM);
	}
	m_arpeggiator.setBPM(p_BPM);
}

void OdinAudioProcessor::setFXButtonsPosition(int p_delay, int p_phaser, int p_flanger, int p_chorus, int p_reverb) {

	m_delay_position   = p_delay;
	m_phaser_position  = p_phaser;
	m_flanger_position = p_flanger;
	m_chorus_position  = p_chorus;
	m_reverb_position  = p_reverb;
}

void OdinAudioProcessor::setFilter3EnvValue() {

	// get adsr value newest voice filterenv
	float env_value = m_adsr[m_voice_manager.getNewestVoiceIndex()][1];

	m_ladder_filter[0].m_env_value  = env_value;
	m_SEM_filter_12[0].m_env_value  = env_value;
	m_korg_filter[0].m_env_value    = env_value;
	m_diode_filter[0].m_env_value   = env_value;
	m_comb_filter[0].m_env_value    = env_value;
	m_formant_filter[0].m_env_value = env_value;
	m_ring_mod[0].m_env_value       = env_value;
	m_ladder_filter[1].m_env_value  = env_value;
	m_SEM_filter_12[1].m_env_value  = env_value;
	m_korg_filter[1].m_env_value    = env_value;
	m_diode_filter[1].m_env_value   = env_value;
	m_comb_filter[1].m_env_value    = env_value;
	m_formant_filter[1].m_env_value = env_value;
	m_ring_mod[1].m_env_value       = env_value;
}

void OdinAudioProcessor::attachNonParamListeners() {
	m_value_tree_fx         = m_value_tree.state.getChildWithName("fx");
	m_value_tree_osc        = m_value_tree.state.getChildWithName("osc");
	m_value_tree_mod        = m_value_tree.state.getChildWithName("mod");
	m_value_tree_misc       = m_value_tree.state.getChildWithName("misc");
	m_value_tree_lfo        = m_value_tree.state.getChildWithName("lfo");
	m_value_tree_draw       = m_value_tree.state.getChildWithName("draw");
	m_value_tree_midi_learn = m_value_tree.state.getChildWithName("midi_learn");
}

void OdinAudioProcessor::setMonoPolyLegato(PlayModes p_mode) {
	m_mono_poly_legato = p_mode;

	bool legato_was_changed = m_voice_manager.setMonoPolyLegato(p_mode);
	for (int voice = 0; voice < VOICES; ++voice) {
		m_voice[voice].setMonoPolyLegato(p_mode);
	}

	if (legato_was_changed) {
		resetAudioEngine();
		m_playmode_mono_note_list.clear();
	}
}