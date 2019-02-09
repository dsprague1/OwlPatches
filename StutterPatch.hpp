////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 
 
 LICENSE:
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 
 */


/* created by the OWL team 2014 */


////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef __StutterPatch_hpp__
#define __StutterPatch_hpp__

#include "StompBox.h"

class StutterPatch : public Patch
{
public:
	StutterPatch() :
		buffer_(0),
		writeIndex_(0),
		readIndex_(0),
		startIndex_(0),
		endIndex_(0),
		stutter_(0),
		rateSlider_(0),
		startSlider_(0),
		mixSlider_(0),
		modeSlider_(0),
		stutterSlider_(0),
		tempo_(120),
		bufferSize_(0x10000)
	{
		registerParameter(PARAMETER_A, "Rate");
		registerParameter(PARAMETER_B, "Start");
		registerParameter(PARAMETER_C, "Mix");
		registerParameter(PARAMETER_D, "Mode");
		registerParameter(PARAMETER_E, "Stutter");

		AudioBuffer* buffer = createMemoryBuffer(1, 0x10000);
		buffer_ = buffer->getSamples(0);
		for (int i = 0; i < 0x10000; i++)
		{
			buffer_[i] = 0;
		}

		/*----- MIDI Note# Pitch Table -----*/
		for (int i = 0; i < 128; i++)
		{
			semiTable_[i] = (float)((440.0 * pow(2.0, (i - 69) / 12.0)));
		}
	}


	void processAudio(AudioBuffer &buffer)
	{
		int numSamples = buffer.getSize();
		float * input = buffer.getSamples(0);
		float * inputR = (buffer.getChannels() == 2) ? buffer.getSamples(1) : nullptr;

		for (int i = 0; i < numSamples; i++)
		{
			const float in = input[i];
			// update coefficients
			if ((i & 0x3f) == 0)
			{
				rateSlider_ = getParameterValue(PARAMETER_A);
				startSlider_ = getParameterValue(PARAMETER_B);
				mixSlider_ = getParameterValue(PARAMETER_C);
				modeSlider_ = getParameterValue(PARAMETER_D);

				updateParams();
			}

			float pdl = getParameterValue(PARAMETER_E);
			if (stutterSlider_ != pdl)
			{
				stutterSlider_ = pdl;
				if (stutterSlider_ > 0)
				{
					stutter_ = true;
					bufferFilled_ = false;
					firstPass_ = true;
					writeIndex_ = 0;
					readIndex_ = 0;
					phaseAcc_ = 0;
				}
				else
				{
					stutter_ = false;
				}
			}

			if (stutter_)
			{
				if (writeIndex_ < bufferSize_)
				{
					buffer_[writeIndex_++] = in;
					if (writeIndex_ >= bufferSize_)
					{
						bufferFilled_ = true;
					}
				}

				float delayOut = buffer_[readIndex_];
				float out = delayOut + (buffer_[readIndex_ + 1] - delayOut) * phaseAcc_;

				input[i] = (firstPass_) ? in : out * mixSlider_ + in * (1.f - mixSlider_);
				if (inputR)
				{
					inputR[i] = input[i];
				}
				
				if (stutterMode_ == kPlayModeRev)
				{
					readIndex_--;
					phaseAcc_ -= phaseInc_;

					if (phaseAcc_ < 1.f)
					{
						phaseAcc_ = 1.f;
					}

					if (endIndex_ < startIndex_)
					{
						// aliased
						readIndex_ = (readIndex_ < 0) ? bufferSize_ - 1 : readIndex_;
						readIndex_ = (readIndex_ < endIndex_ && readIndex_ > startIndex_) ? endIndex_ : readIndex_;
						firstPass_ = false;
					}
					else
					{
						readIndex_ = (readIndex_ < startIndex_) ? endIndex_ : readIndex_;
						firstPass_ = false;
					}
				}
				else if (stutterMode_ == kPlayModeFwd )
				{
					readIndex_++;
					phaseAcc_ += phaseInc_;
					if (phaseAcc_ > 1.f)
					{
						phaseAcc_ = 0;
					}
					if (endIndex_ < startIndex_)
					{
						readIndex_ = (readIndex_ >= bufferSize_) ? 0 : readIndex_;
						readIndex_ = (readIndex_ >= endIndex_ && readIndex_ < startIndex_) ? startIndex_ : readIndex_;
						firstPass_ = false;
					}
					else
					{
						readIndex_ = (readIndex_ > endIndex_) ? startIndex_ : readIndex_;
						firstPass_ = false;
					}
				}
			}
		}
	}

	// for tap tempo 
	virtual void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) 
	{
		int16_t delta = samples - previousButtonSample_;
		float timePassed = float(delta) / getSampleRate();
		float avg = timePassed;

		// average
		for (int i = 0; i < 4; i++)
		{
			avg += tempoCalcMemory_[i];
		}
		avg *= 0.20;

		//shift
		tempoCalcMemory_[3] = tempoCalcMemory_[2];
		tempoCalcMemory_[2] = tempoCalcMemory_[1];
		tempoCalcMemory_[1] = tempoCalcMemory_[0];
		tempoCalcMemory_[0] = timePassed;

		tempo_ = avg * 60;

		previousButtonSample_ = samples;
	}
private:

	float noteValues_[16] =
	{
		3.75,   7.5,    10,     15,     // 1/64, 1/32, 1/24, 1/16
		20,     30,     40,     45,     // 1/12, 1/18, 1/6,  3/16
		60,     80,     90,     120,    // 1/4,  1/3,  3/8,  1/2
		180,    240,    480,    960,    // 3/4,  1/1,  2/1,  4/1
	};

	void updateParams()
	{
		stutterMode_ = uint32_t(modeSlider_ * (kNumPlayMode - 1) + 0.5f);

		int scaledValue = (1.f - rateSlider_) * 15;

		float stutterTimeMs = CookStutterTime((int)scaledValue);
		float stutterTimeSamples = stutterTimeMs * getSampleRate() / 1000.f;
		int mask = (int)stutterTimeSamples;
		phaseInc_ = stutterTimeSamples - mask;
		phaseAcc_ = 0;

		startIndex_ = uint32_t(startSlider_ * float(mask));
		endIndex_ = startIndex_ + mask;
		if (endIndex_ > bufferSize_)
		{
			endIndex_ = endIndex_ - bufferSize_;
		}
	}

	float CookStutterTime(int startingVal)
	{
		float stutterTime = (noteValues_[startingVal] / float(tempo_)) * 1000.0;
		while (stutterTime > ((float)(bufferSize_ - 1) / getSampleRate()) * 1000)
		{
			stutterTime = (noteValues_[startingVal] / float(tempo_)) * 1000.0;
			startingVal--;
		}
		return stutterTime;
	}

	enum
	{
		kPlayModeFwd,
		kPlayModeRev,
		kNumPlayMode
	};

	// data
	float	 tempo_;
	float    phaseAcc_;
	float	 phaseInc_;
	float	 tempoCalcMemory_[4];
	float	 semiTable_[128 + 12];
	float  * buffer_;
	uint16_t previousButtonSample_;
	uint32_t writeIndex_;
	uint32_t readIndex_;
	uint32_t startIndex_;
	uint32_t endIndex_;
	uint32_t stutterMode_;
	bool     stutter_;
	bool	 bufferFilled_;
	bool     firstPass_;

	// param
	float rateSlider_;
	float startSlider_;
	float mixSlider_;
	float modeSlider_;
	float stutterSlider_;

	// consts
	const uint32_t bufferSize_;
};
#endif // __TemplatePatch_hpp__
