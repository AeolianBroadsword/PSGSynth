#include <midiport.h>
#include <SoftwareSerial.h>
#include <MidiUtils.h>
#include <PSG.h>

using namespace MidiUtils;

SoftwareSerial midiSerial(10, 11, true);
MidiPort midi(midiSerial);
//MidiPort midi(Serial);

MonoPolyMode monoPolyMode = MonoPolyMode::Mono;

int midiVelocityToVolume(int velocity) {
  return (velocity&0x7f)/8;
}

struct SendBytePins : public SendByte {
  const int PSG_WE = A0;
  static constexpr char dataPins[] = {8, 7, 6, 5, 4, A4, A3, A2};
  virtual void operator()(byte b) {  
    for (int i = 0; i<8; ++i) {
      digitalWrite(dataPins[i], (b >> i)&1);
    }
    delay(1);
    digitalWrite(PSG_WE, LOW);
    delay(1);
    digitalWrite(PSG_WE, HIGH);
  }

  void Setup() {
    for (int i = 0; i<8; ++i) {
      pinMode(dataPins[i], OUTPUT);
    }
    pinMode(PSG_WE, OUTPUT);
    digitalWrite(PSG_WE, HIGH);
  }
};
static constexpr char SendBytePins::dataPins[8];

SendBytePins sendBytePins;
PSG psg(sendBytePins);

void setupClock() {
  #ifdef __AVR_ATmega2560__
    const byte CLOCKOUT = 11;  // Mega 2560
  #else
    const byte CLOCKOUT = 9;   // Uno, Duemilanove, etc.
  #endif
  
  // set up 8 MHz timer on CLOCKOUT (OC1A)
  pinMode (CLOCKOUT, OUTPUT); 
  // set up Timer 1
  TCCR1A = bit (COM1A0);  // toggle OC1A on Compare Match
  TCCR1B = bit (WGM12) | bit (CS10);   // CTC, no prescaling
  OCR1A = 3;       // output every other 4th cycle
}

unsigned long previousTime;

void setup() {
  Serial.begin(9600);
  midiSerial.begin(31250);

  setupClock();

  previousTime = millis();

  sendBytePins.Setup();

  psg.setVolume(PSG::CH_1, 15);
  psg.setVolume(PSG::CH_1, 0);
  psg.setVolume(PSG::CH_2, 0);
  psg.setVolume(PSG::CH_3, 0);
  psg.setVolume(PSG::CH_4, 0);
}

MidiUtils::Envelope envelopes[4];

byte portamentoTime = 0;

struct ActiveNote;
void updateEnvelope(ActiveNote& activeNote, int channel);

struct ActiveNote {
  byte note;
  byte velocity;
  unsigned long startTime;
  byte state;
  unsigned long stateElapsedTime;
  byte synthLevel;

  void NoteOn(byte midiNote, byte velocity, const Envelope& envelope) {
    note = midiNote;
    this->velocity = velocity;
    startTime = millis();
    stateElapsedTime = 0;
    
    if (envelope.attack != 0) {
      this->state = Envelope::State::Attack;
    } else if (envelope.decay != 0) {
      this->state = Envelope::State::Decay;
    } else {
      this->state = Envelope::State::Sustain;
    }
  }

  void NoteOff(const Envelope& envelope) {
    if (envelope.release != 0) {
      this->state = Envelope::State::Release;
    } else {
      this->state = Envelope::State::Init;
      this->note = 0;
    }
    stateElapsedTime = 0;
  }

  void Stop() {
    this->state = Envelope::State::Init;
    this->note = 0;
    stateElapsedTime = 0;
  }

  void Update(unsigned long elapsed, const Envelope& envelope) {
    this->stateElapsedTime += elapsed;
    
    if (this->state == Envelope::State::Attack) {
      if (this->stateElapsedTime > envelope.attackTime()) {
        stateElapsedTime = 0;
        if (envelope.decay > 0) {
          this->state = Envelope::State::Decay;
        } else {
          this->state = Envelope::State::Sustain;
        }
      }
    }

    if (this->state == Envelope::State::Decay) {
      if (this->stateElapsedTime > envelope.decayTime()) {
        stateElapsedTime = 0; 
        this->state = Envelope::State::Sustain;
      }
    }

    if (this->state == Envelope::State::Release) {
      if (this->stateElapsedTime > envelope.releaseTime()) {
        stateElapsedTime = 0;
        this->state = Envelope::State::Init;
        this->note = 0;
      }
    }
  }

  byte GetLevel(const Envelope& envelope) {
    if (this->state == Envelope::State::Init) {
      return 0;
    } else if (this->state == Envelope::State::Attack) {
      float x = 1.0f * this->stateElapsedTime / envelope.attackTime();
      return this->velocity * x;
    } else if (this->state == Envelope::State::Decay) {
      float x = 1.0f * this->stateElapsedTime / envelope.decayTime();
      byte sustainLevel = (int)this->velocity * (int)envelope.sustain / 127;
      return this->velocity - (this->velocity - sustainLevel)*(x);
    } else if (this->state == Envelope::State::Sustain) {
      return (int)this->velocity * (int)envelope.sustain / 127;
    } else if (this->state == Envelope::State::Release) {
      float x = 1.0f * this->stateElapsedTime / envelope.releaseTime();
      byte sustainLevel = (int)this->velocity * (int)envelope.sustain / 127;
      return sustainLevel*(1.0f-x);
    }
  }
};

ActiveNote activeNotes[3];

void updatePortamento() {
  if (portamentoTime == 0) {
    return;
  }
  int pmms = portamentoTime * 1000L / 127;
  unsigned long currentTime = millis();

  for (int i = 0; i<3; ++i) {
    unsigned long noteElapsed = currentTime - activeNotes[i].startTime;
    if (noteElapsed <= pmms && activeNotes[i].note > 0) {
      float x = (float)noteElapsed / (float)pmms;
      psg.setTone(i, midiNoteToFreq((float)activeNotes[i].note - (1.0f-x)*2));
    }
  }
}

void updateEnvelopes() {
  unsigned long currentTime = millis();
  unsigned long elapsed = currentTime - previousTime;
  
  for (int i = 0; i<3; ++i) {
    activeNotes[i].Update(elapsed, envelopes[i]);
  }
  for (int i = 0; i<3; ++i) {
    updateEnvelope(activeNotes[i], i);
  }

  previousTime = currentTime;
}

void updateEnvelope(ActiveNote& activeNote, int channel) {
  byte level = activeNote.GetLevel(envelopes[channel]);
  byte psgLevel = midiVelocityToVolume(level);
  if (psgLevel != activeNote.synthLevel) {
    psg.setVolume(channel, psgLevel);
    activeNote.synthLevel = psgLevel;
  }
}

int findFreeNoteSlot(int channel) {
  if (monoPolyMode == MonoPolyMode::Mono) {
    return channel;
  }
  for (int i=0; i<3; ++i) {
    if (activeNotes[i].note == 0) {
      return i;
    }
  }
  return -1;
}

int findActiveNote(int note, int channel) {
  if (monoPolyMode == MonoPolyMode::Mono) {
    return channel;
  }
  for (int i=0; i<3; ++i) {
    if (activeNotes[i].note == note) {
      return i;
    }
  }
  return -1;
}

void noteOff(int note, int channel) {
  int slot = findActiveNote(note, channel);
  if (slot >= 0 && activeNotes[slot].note == note) {
    activeNotes[slot].NoteOff(envelopes[channel]);
  }
}

void loop() {
  //delay(1);
  while (midi.message_available() > 0) {
    MidiMessage m = midi.read_message();
    if (m.command == Command::NOTE_ON) {
      if (m.channel == 9) {
        midi.comment("Note on channel 9");
        psg.setVolume(PSG::CH_4, midiVelocityToVolume(m.param2));
        if (m.param1 == 36) {
          midi.comment("Setting Noise Low");
          psg.setNoise(PSG::White, PSG::Low);
        } else if (m.param1 == 38) {
          psg.setNoise(PSG::White, PSG::Mid);
        } else if (m.param1 == 42) {
          psg.setNoise(PSG::White, PSG::High);
        }
      } else {
        if (m.velocity() > 0) {
          int slot = findActiveNote(m.note(), m.channel);
          if (slot < 0) {
            slot = findFreeNoteSlot(m.channel);
          }
          if (slot >= 0) {
            psg.setTone(slot, midiNoteToFreq(m.note()));
            activeNotes[slot].NoteOn(m.note(), m.velocity(), envelopes[slot]);
          }
        } else {
          noteOff(m.note(), m.channel);
        }
      }
    } else if (m.command == Command::NOTE_OFF) {
      if (m.channel == 9) {
        psg.setVolume(PSG::CH_4, 0);
      } else {
        noteOff(m.note(), m.channel);
      }
    } else if (m.command == Command::PITCH_BEND) {
      float bend = (m.pitchBend() - 8192)/4096.0f;
      psg.setTone(m.channel, midiNoteToFreq((float)activeNotes[m.channel].note + bend));
    } else if (m.command == Command::CONTROLLER_CHANGE) {
      if (m.param1 == 120 || m.param1 == 123) {  // Stop all notes, stop all sound
        midi.comment("Stop all");
        for (int i = 0; i<4; ++i) {
          psg.setVolume(i, 0);
        }
        for (int i = 0; i<3; ++i) {
          activeNotes[i].Stop();
        }
      } else if (m.param1 == 72) {
        envelopes[m.channel].release = m.param2;
      } else if (m.param1 == 73) {
        envelopes[m.channel].attack = m.param2;
      } else if (m.param1 == 75) {
        envelopes[m.channel].decay = m.param2;
      } else if (m.param1 == 80) {
        envelopes[m.channel].sustain = m.param2;
      } else if (m.param1 == 5) {
        portamentoTime = m.param2;
      } else if (m.param1 == 126) {
        monoPolyMode = MonoPolyMode::Mono;
      } else if (m.param1 == 127) {
        monoPolyMode = MonoPolyMode::Poly;
      }
    }
  }

  updateEnvelopes();
  updatePortamento();
}
