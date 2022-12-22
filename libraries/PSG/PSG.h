#ifndef PSG_H__
#define PSG_H__

struct SendByte {
  virtual void operator()(byte b) = 0;
};

class PSG {
  public:
    enum Channel {
    CH_1 = 0,
    CH_2 = 1,
    CH_3 = 2,
    CH_4 = 3
  };
  
  enum NoiseType {
    Periodic = 0,
    White = 1
  };
  
  enum NoiseCtrl {
    High = 0,
    Mid = 1,
    Low = 2,
    CH3 = 3
  };
  
  void setVolume(Channel channel, byte level) {
    sendByte((1<<7) | (channel<<5) | (1<<4) | (15-level));
  }
  
  void setTone(Channel channel, int frequency) {
    int tenbit = 62500/frequency;
    int lowBits = tenbit & 0xF;
    int highBits = tenbit >> 4;
    sendByte((1<<7) | (channel<<5) | lowBits);
    sendByte(highBits);
  }
  
  void setNoise(byte type, byte freqCtrl) {
    setNoise((type << 2) | freqCtrl);
  }

  PSG(SendByte& _sendByte) : sendByte(_sendByte) {}

  private:
  SendByte& sendByte;
  
  void setNoise(byte typeFreq) {
    sendByte((1<<7) | (3<<5) | typeFreq);
  }
};

#endif