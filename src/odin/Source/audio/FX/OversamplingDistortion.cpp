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

#include "OversamplingDistortion.h"
#include "../../GlobalIncludes.h"
#include <cmath>

OversamplingDistortion::OversamplingDistortion() {
}

OversamplingDistortion::~OversamplingDistortion() {
}

double OversamplingDistortion::doDistortion(double p_input) {

	// do linear interpolation
	double input_upsampled[3] = {
	    0.66666666 * m_last_input + 0.33333333 * p_input, 0.33333333 * m_last_input + 0.66666666 * p_input, p_input};

	m_last_input = p_input;

	m_threshold_smooth =
	    m_threshold_smooth * THRESHOLD_SMOOTHIN_FACTOR + (1 - THRESHOLD_SMOOTHIN_FACTOR) * (m_threshold);

	//theshold is now boost, so we need to subtract mod (control setter is inverted as well)
	float threshold_modded = (m_threshold_smooth - *m_threshold_mod) * (1.f - THRESHOLD_MIN) + THRESHOLD_MIN;
	threshold_modded       = threshold_modded > 1 ? 1 : threshold_modded;
	threshold_modded       = threshold_modded < THRESHOLD_MIN ? THRESHOLD_MIN : threshold_modded;

	// odin2-mpc ECO Lite: just the current sample, no oversampling (see setLite)
	const int n_up = m_lite ? 1 : 3;
	if (m_lite) {
		input_upsampled[0] = p_input;
	}

	// do distortion
	switch (m_algorithm) {
	case Clamp:
		for (int sample = 0; sample < n_up; ++sample) {
			if (input_upsampled[sample] > m_bias && input_upsampled[sample] > m_bias + threshold_modded) {
				input_upsampled[sample] = m_bias + threshold_modded;
			} else if (input_upsampled[sample] < m_bias && input_upsampled[sample] < m_bias - threshold_modded) {
				input_upsampled[sample] = m_bias - threshold_modded;
			}
		}
		break;
	case Zero:
		// half "boost" for zero
		threshold_modded = 0.5f + threshold_modded * 0.5f;
		for (int sample = 0; sample < n_up; ++sample) {
			if (input_upsampled[sample] > m_bias && input_upsampled[sample] > m_bias + threshold_modded) {
				input_upsampled[sample] = 0.;
			} else if (input_upsampled[sample] < m_bias && input_upsampled[sample] < m_bias - threshold_modded) {
				input_upsampled[sample] = 0.;
			}
		}
		break;
	case Sine:
		for (int sample = 0; sample < n_up; ++sample) {
			input_upsampled[sample] = sin(input_upsampled[sample] /*/ threshold_modded*/);
		}
	case Cube:
		for (int sample = 0; sample < n_up; ++sample) {
			//input_upsampled[sample] /= threshold_modded;
			input_upsampled[sample] *= input_upsampled[sample] * input_upsampled[sample];
		}
		break;
	case Fold:
		for (int sample = 0; sample < n_up; ++sample) {
			//threshold_modded = threshold_modded < 0.05 ? 0.05 : threshold_modded;
			while (fabs(input_upsampled[sample]) > threshold_modded) {
				if (input_upsampled[sample] > threshold_modded) {
					input_upsampled[sample] = 2 * threshold_modded - input_upsampled[sample];
				} else {
					input_upsampled[sample] = -2 * threshold_modded - input_upsampled[sample];
				}
			}
		}
		break;
	}

	double filtered;
	if (m_lite) {
		filtered = input_upsampled[0];
	} else {
	// odin2-mpc: the same 9th-order Butterworth as upstream (same terms in the same order, so the same output),
	// with its history in local variables for the three steps instead of shifting the member arrays three times.
	double x0 = xv[1], x1 = xv[2], x2 = xv[3], x3 = xv[4], x4 = xv[5], x5 = xv[6], x6 = xv[7], x7 = xv[8], x8 = xv[9], x9;
	double y0 = yv[1], y1 = yv[2], y2 = yv[3], y3 = yv[4], y4 = yv[5], y5 = yv[6], y6 = yv[7], y7 = yv[8], y8 = yv[9], y9;
#define ODIN_MPC_DOWNSAMPLE_STEP(in)                                                                                   \
	x9 = (in) * 0.019966841051093;                                                                                     \
	y9 = (x0 + x9) + 9 * (x1 + x8) + 36 * (x2 + x7) + 84 * (x3 + x6) + 126 * (x4 + x5) + (-0.0003977153 * y0) +        \
	     (-0.0064474617 * y1) + (-0.0476997403 * y2) + (-0.2185829743 * y3) + (-0.6649234123 * y4) +                   \
	     (-1.4773657709 * y5) + (-2.2721421641 * y6) + (-2.6598673212 * y7) + (-1.8755960587 * y8);
#define ODIN_MPC_DOWNSAMPLE_SHIFT                                                                                      \
	x0 = x1; x1 = x2; x2 = x3; x3 = x4; x4 = x5; x5 = x6; x6 = x7; x7 = x8; x8 = x9;                                    \
	y0 = y1; y1 = y2; y2 = y3; y3 = y4; y4 = y5; y5 = y6; y6 = y7; y7 = y8; y8 = y9;
	ODIN_MPC_DOWNSAMPLE_STEP(input_upsampled[0])
	ODIN_MPC_DOWNSAMPLE_SHIFT
	ODIN_MPC_DOWNSAMPLE_STEP(input_upsampled[1])
	ODIN_MPC_DOWNSAMPLE_SHIFT
	ODIN_MPC_DOWNSAMPLE_STEP(input_upsampled[2])
#undef ODIN_MPC_DOWNSAMPLE_STEP
#undef ODIN_MPC_DOWNSAMPLE_SHIFT
	// store the history as upstream leaves it: xv/yv[0..9] = the last ten values, newest last
	xv[0] = x0; xv[1] = x1; xv[2] = x2; xv[3] = x3; xv[4] = x4; xv[5] = x5; xv[6] = x6; xv[7] = x7; xv[8] = x8; xv[9] = x9;
	yv[0] = y0; yv[1] = y1; yv[2] = y2; yv[3] = y3; yv[4] = y4; yv[5] = y5; yv[6] = y6; yv[7] = y7; yv[8] = y8; yv[9] = y9;
	filtered = y9;
	}

	float drywet_modded = m_drywet + *m_drywet_mod;
	drywet_modded       = drywet_modded > 1 ? 1 : drywet_modded;
	drywet_modded       = drywet_modded < 0 ? 0 : drywet_modded;

	// return only the last of the three samples
	switch (m_algorithm) {
	case Clamp:
	case Fold:
	case Zero:
		return filtered * drywet_modded / threshold_modded * DISTORTION_OUTPUT_SCALAR + p_input * (1.f - drywet_modded);
	case Sine:
	case Cube:
		return filtered * drywet_modded + p_input * (1.f - drywet_modded);
	default:
		return p_input;
		break;
	}
}
