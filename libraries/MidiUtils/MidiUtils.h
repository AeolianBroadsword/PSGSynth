#ifndef MIDI_UTILS_H__
#define MIDI_UTILS_H__

namespace MidiUtils {
	float midiNoteToFreq(float midiNote) {
		return 440.0f*pow(2.0f, (midiNote-69.0f)/12.0f);
	}

	int midiNoteToFreq(int midiNote) {
	  return 440.0*pow(2, (midiNote - 69)/12.0);
	}
	
	struct MonoPolyMode {
		enum {
			Mono,
			Poly
		};
		int value;
		MonoPolyMode& operator=(int v) {
			value = v;
			return *this;
		}
		MonoPolyMode(int v): value(v) {}
		bool operator==(int v) {
			return value == v;
		}
	};
	
	struct Envelope {
		struct State {
			enum {
				Init = 0,
				Attack = 1,
				Decay = 2,
				Sustain = 3,
				Release = 4
			};
		};
		byte attack;
		byte decay;
		byte sustain;
		byte release;

		int attackTime() {
			return 1000L * attack / 127;
		}

		int decayTime() {
			return 1000L * decay / 127;
		}

		int releaseTime() {
			return 1000L * release / 127;
		}
  
		Envelope()
		: attack(0)
		, decay(0)
		, sustain(127)
		, release(0) {}
  
	};
}

#endif