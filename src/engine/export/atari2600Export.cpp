/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2022 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "atari2600Export.h"

#include <fmt/printf.h>
#include <set>
#include "../../ta-log.h"

const unsigned int AUDC0 = 0x15;
const unsigned int AUDC1 = 0x16;
const unsigned int AUDF0 = 0x17;
const unsigned int AUDF1 = 0x18;
const unsigned int AUDV0 = 0x19;
const unsigned int AUDV1 = 0x1A;

std::map<unsigned int, unsigned int> channel0AddressMap = {
  {AUDC0, 0},
  {AUDF0, 1},
  {AUDV0, 2},
};

std::map<unsigned int, unsigned int> channel1AddressMap = {
  {AUDC1, 0},
  {AUDF1, 1},
  {AUDV1, 2},
};

std::vector<DivROMExportOutput> DivExportAtari2600::go(DivEngine* e) {
  std::vector<DivROMExportOutput> ret;

  // capture all sequences
  logD("performing sequence capture");
  std::vector<String> channelSequences[2];
  std::map<String, DumpSequence> registerDumps;
  captureSequence(e, 0, 0, DIV_SYSTEM_TIA, channel0AddressMap, channelSequences[0], registerDumps);
  captureSequence(e, 0, 1, DIV_SYSTEM_TIA, channel1AddressMap, channelSequences[1], registerDumps);

  // scrunch the register dumps with 0 volume
  for (auto& x: registerDumps) {
      for (auto& y: x.second.intervals) {
        logD("checking 0 volume interval %s %d %d %d %d", x.first, y.state.registers[0], y.state.registers[1], y.state.registers[2], y.duration);
        if (0 == y.state.registers[2]) {
          logD("found 0 volume interval");
          y.state.registers[0] = 0;
          y.state.registers[1] = 0;
        }
      }
  }

  // compress the patterns into common subsequences
  logD("performing sequence compression");
  std::map<uint64_t, String> commonDumpSequences;
  std::map<uint64_t, unsigned int> frequencyMap;
  std::map<String, String> representativeMap;
  findCommonDumpSequences(
    registerDumps,
    commonDumpSequences,
    frequencyMap,
    representativeMap);

  writeTrackV0(
    e,
    channelSequences,
    registerDumps,
    ret
  );
  writeTrackV1(
    e,
    commonDumpSequences,
    frequencyMap,
    representativeMap,
    registerDumps,
    ret
  );
  writeTrackV2(
    e,
    commonDumpSequences,
    representativeMap,
    channelSequences,
    registerDumps,
    ret
  );


  // create meta data (optional)
  logD("writing track title graphics");
  SafeWriter* titleData=new SafeWriter;
  titleData->init();
  titleData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  titleData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));
  auto title = (e->song.name.length() > 0) ?
     (e->song.name + " by " + e->song.author) :
     "furnace tracker";
  if (title.length() > 26) {
    title = title.substr(23) + "...";
  }
  writeTextGraphics(titleData, title.c_str());
  ret.push_back(DivROMExportOutput("Track_meta.asm", titleData));

  return ret;

}

void DivExportAtari2600::writeTrackV0(
  DivEngine* e,
  std::vector<String> *channelSequences,
  std::map<String, DumpSequence> &registerDumps,
  std::vector<DivROMExportOutput> &ret
) {

  // logD("writing raw binary audio data");
  // SafeWriter* binaryData=new SafeWriter;
  // binaryData->init();
  // ChannelState last(255);
  // for (int i = 0; i < 2; i++) {
  //   for (auto x : channelSequences[i]) {
  //     auto& dump = registerDumps.at(x);
  //     for (auto& n: dump.intervals) {
  //       writeNoteBinary(binaryData, n.state, n.duration, last);
  //       last = n.state;
  //     }
  //   }
  // }
  // ret.push_back(DivROMExportOutput("Track_binary.bin", binaryData));

  logD("writing raw audio data");
  SafeWriter* rawData=new SafeWriter;
  rawData->init();
  for (int i = 0; i < 2; i++) {
    unsigned char lastfx, lastvx, lastcx;
    int duration = 0;
    int note = 0;
    int track = 0;
    for (auto& x : channelSequences[i]) {
      if (0 == note) {
        rawData->writeText("    byte 0\n");
        rawData->writeText(fmt::sprintf("TRACK_TITLE_%d_C0%d = . - AUDIO_TRACKS\n", track, i));
        track = track + 1;
      }
      rawData->writeText(fmt::sprintf("%s\n", x.c_str()));
      auto& dump = registerDumps.at(x);
      for (auto& n : dump.intervals) {

        //  Write note data. Format:
        // 
        //   00000000                    stop 
        //   ddddddd1                    pause + duration
        //   fffff010                    frequency, duration 1
        //   fffff100                    frequency, duration 2
        //   fffff110 wwwwdddd           frequency + waveform + duration d

        unsigned char audcx, audfx, audvx;
        int ac, fc, vc;
        audcx = n.state.registers[0];
        ac = audcx != lastcx;
        audfx = n.state.registers[1];
        fc = audfx != lastfx;
        audvx = n.state.registers[2];
        vc = audvx != lastvx;
        
        if ((0 == note) || ac || fc || vc) {
          while (duration > 0) {
            unsigned char dx = duration;
            // BUGBUG: make sure duration < 16
            unsigned char rx, ex;
            if (lastvx == 0) {
              if (dx > 127) {
                dx = 127;
              }
              rawData->writeText(fmt::sprintf("    ; PAUSE, D:%d\n", dx));
              rx = dx << 1 | 0x01;
              rawData->writeText(fmt::sprintf("    byte %d\n", rx));
            } else if (ac || fc || duration > 2) {
              if (dx > 31) {
                dx = 31; // BUGBUG: KLUDGE
              }
              rawData->writeText(fmt::sprintf("    ; CX:%d, FX:%d, VX:%d, D:%d\n", lastcx, lastfx, lastvx, dx));
              rx = lastfx << 3 | 0x06;
              ex = lastcx << 4 | dx; // BUGBUG: KLUDGE
              rawData->writeText(fmt::sprintf("    byte %d, %d\n", rx, ex));
            } else {
              rawData->writeText(fmt::sprintf("    ; CX:%d, FX:%d, VX:%d, D:%d\n", lastcx, lastfx, lastvx, dx));
              rx = lastfx << 3 | (duration > 1 ? 0x40 : 0x20);
              rawData->writeText(fmt::sprintf("    byte %d\n", rx));
            }
            duration = duration - dx;
          }
        }

        lastcx = audcx;
        lastfx = audfx;
        lastvx = audvx;

        duration += n.duration;
      }
      note = (note + 1) % 24;
    }
    while (duration > 0) {
      unsigned char dx = duration;
      // BUGBUG: make sure duration < 16
      unsigned char rx, ex;
      if (lastvx == 0) {
        if (dx > 127) {
          dx = 127;
        }
        rawData->writeText(fmt::sprintf("    ; PAUSE, D:%d\n", dx));
        rx = dx << 1 | 0x01;
        rawData->writeText(fmt::sprintf("    byte %d\n", rx));
      } else if (duration > 2) {
        if (dx > 31) {
          dx = 31; // BUGBUG: KLUDGE
        }
        rawData->writeText(fmt::sprintf("    ; CX:%d, FX:%d, VX:%d, D:%d\n", lastcx, lastfx, lastvx, dx));
        rx = lastfx << 3 | 0x06;
        ex = lastcx << 4 | dx; // BUGBUG: KLUDGE
        rawData->writeText(fmt::sprintf("    byte %d, %d\n", rx, ex));
      } else {
        rawData->writeText(fmt::sprintf("    ; CX:%d, FX:%d, VX:%d, D:%d\n", lastcx, lastfx, lastvx, dx));
        rx = lastfx << 3 | (duration > 1 ? 0x40 : 0x20);
        rawData->writeText(fmt::sprintf("    byte %d\n", rx));
      }
      duration = duration - dx;
    }
    rawData->writeText("    byte 0\n");
  }
  ret.push_back(DivROMExportOutput("Track_raw.asm", rawData));
}

void DivExportAtari2600::writeTrackV1(
  DivEngine* e, 
  std::map<uint64_t, String> &commonDumpSequences,
  std::map<uint64_t, unsigned int> &frequencyMap,
  std::map<String, String> &representativeMap,
  std::map<String, DumpSequence> &registerDumps,
  std::vector<DivROMExportOutput> &ret
) {

  // create track data
  logD("writing track audio data");
  SafeWriter* trackData=new SafeWriter;
  trackData->init();
  trackData->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  trackData->writeText(fmt::sprintf("; Author: %s\n", e->song.author));


  // emit song table
  logD("writing song table");
  size_t songTableSize = 0;
  trackData->writeText("\n; Song Lookup Table\n");
  trackData->writeText(fmt::sprintf("NUM_SONGS = %d\n", e->song.subsong.size()));
  trackData->writeText("SONG_TABLE_START_LO\n");
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    trackData->writeText(fmt::sprintf("SONG_%d = . - SONG_TABLE_START_LO\n", i));
    trackData->writeText(fmt::sprintf("    byte <SONG_%d_ADDR\n", i));
    songTableSize++;
  }
  trackData->writeText("SONG_TABLE_START_HI\n");
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    trackData->writeText(fmt::sprintf("    byte >SONG_%d_ADDR\n", i));
    songTableSize++;
  }

  // collect and emit song data
  // borrowed from fileops
  size_t songDataSize = 0;
  trackData->writeText("; songs\n");
  std::vector<PatternIndex> patterns;
  const int channelCount = 2;
  bool alreadyAdded[channelCount][256];
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    trackData->writeText(fmt::sprintf("SONG_%d_ADDR\n", i));
    DivSubSong* subs = e->song.subsong[i];
    memset(alreadyAdded, 0, 2*256*sizeof(bool));
    for (int j = 0; j < subs->ordersLen; j++) {
      trackData->writeText("    byte ");
      for (int k = 0; k < channelCount; k++) {
        if (k > 0) {
          trackData->writeText(", ");
        }
        unsigned short p = subs->orders.ord[k][j];
        logD("ss: %d ord: %d chan: %d pat: %d", i, j, k, p);
        String key = getPatternKey(i, k, p);
        trackData->writeText(key);
        songDataSize++;
        if (alreadyAdded[k][p]) continue;
        patterns.push_back(PatternIndex(key, i, j, k, p));
        alreadyAdded[k][p] = true;
      }
      trackData->writeText("\n");
    }
    trackData->writeText("    byte 255\n");
    songDataSize++;
  }

  // pattern lookup
  size_t patternTableSize = 0;
  trackData->writeC('\n');
  trackData->writeText("; Pattern Lookup Table\n");
  trackData->writeText(fmt::sprintf("NUM_PATTERNS = %d\n", patterns.size()));
  trackData->writeText("PAT_TABLE_START_LO\n");
  for (PatternIndex& patternIndex: patterns) {
    trackData->writeText(fmt::sprintf("%s = . - PAT_TABLE_START_LO\n", patternIndex.key.c_str()));
    trackData->writeText(fmt::sprintf("   byte <%s_ADDR\n", patternIndex.key.c_str()));
    patternTableSize++;
  }
  trackData->writeText("PAT_TABLE_START_HI\n");
  for (PatternIndex& patternIndex: patterns) {
    trackData->writeText(fmt::sprintf("   byte >%s_ADDR\n", patternIndex.key.c_str()));
    patternTableSize++;
  }

  // emit sequences
  // we emit the "note" being played as an assembly variable 
  // later we will figure out what we need to emit as far as TIA register settings
  // this assumes the song has a limited number of unique "notes"
  size_t patternDataSize = 0;
  for (PatternIndex& patternIndex: patterns) {
    DivPattern* pat = e->song.subsong[patternIndex.subsong]->pat[patternIndex.chan].getPattern(patternIndex.pat, false);
    trackData->writeText(fmt::sprintf("; Subsong: %d Channel: %d Pattern: %d / %s\n", patternIndex.subsong, patternIndex.chan, patternIndex.pat, pat->name));
    trackData->writeText(fmt::sprintf("%s_ADDR", patternIndex.key.c_str()));
    for (int j = 0; j<e->song.subsong[patternIndex.subsong]->patLen; j++) {
      if (j % 8 == 0) {
        trackData->writeText("\n    byte ");
      } else {
        trackData->writeText(",");
      }
      String key = getSequenceKey(patternIndex.subsong, patternIndex.ord, j, patternIndex.chan);
      trackData->writeText(representativeMap[key]); // the representative
      patternDataSize++;
    }
    trackData->writeText("\n    byte 255\n");
    patternDataSize++;
  }

  // emit waveform table
  // this is where we can lookup specific instrument/note/octave combinations
  // can be quite expensive to store this table (2 bytes per waveform)
  size_t waveformTableSize = 0;
  trackData->writeC('\n');
  trackData->writeText("; Waveform Lookup Table\n");
  trackData->writeText(fmt::sprintf("NUM_WAVEFORMS = %d\n", commonDumpSequences.size()));
  trackData->writeText("WF_TABLE_START_LO\n");
  for (auto& x: commonDumpSequences) {
    trackData->writeText(fmt::sprintf("%s = . - WF_TABLE_START_LO\n", x.second.c_str()));
    trackData->writeText(fmt::sprintf("   byte <%s_ADDR\n", x.second.c_str()));
    waveformTableSize++;
  }
  trackData->writeText("WF_TABLE_START_HI\n");
  for (auto& x: commonDumpSequences) {
    trackData->writeText(fmt::sprintf("   byte >%s_ADDR\n", x.second.c_str()));
    waveformTableSize++;
  }
    
  // emit waveforms
  size_t waveformDataSize = 0;
  trackData->writeC('\n');
  trackData->writeText("; Waveforms\n");
  for (auto& x: commonDumpSequences) {
    auto freq = frequencyMap[x.first];
    writeWaveformHeader(trackData, x.second.c_str());
    trackData->writeText(fmt::sprintf("; Hash %d, Freq %d\n", x.first, freq));
    auto& dump = registerDumps[x.second];
    ChannelState last(255);
    for (auto& n: dump.intervals) {
      waveformDataSize += writeNote(trackData, n.state, n.duration, last);
      last = n.state;
    }
    trackData->writeText("    byte 0\n");
    waveformDataSize++;
  }

  // audio metadata
  trackData->writeC('\n');
  trackData->writeText(fmt::sprintf("; Song Table Size %d\n", songTableSize));
  trackData->writeText(fmt::sprintf("; Song Data Size %d\n", songDataSize));
  trackData->writeText(fmt::sprintf("; Pattern Lookup Table Size %d\n", patternTableSize));
  trackData->writeText(fmt::sprintf("; Pattern Data Size %d\n", patternDataSize));
  trackData->writeText(fmt::sprintf("; Waveform Lookup Table Size %d\n", waveformTableSize));
  trackData->writeText(fmt::sprintf("; Waveform Data Size %d\n", waveformDataSize));
  size_t totalDataSize = 
    songTableSize + songDataSize + patternTableSize + 
    patternDataSize + waveformTableSize + waveformDataSize;
  trackData->writeText(fmt::sprintf("; Total Data Size %d\n", totalDataSize));

  ret.push_back(DivROMExportOutput("Track_data.asm", trackData));

}

void DivExportAtari2600::writeTrackV2(
  DivEngine* e, 
  std::map<uint64_t, String> &commonDumpSequences,
  std::map<String, String> &representativeMap,
  std::vector<String> *channelSequences,
  std::map<String, DumpSequence> &registerDumps,
  std::vector<DivROMExportOutput> &ret
) {
  // TODO: principled raw sequence
  std::vector<AlphaCode> alphabet;
  std::map<String, AlphaChar> index;
  createAlphabet(
    commonDumpSequences,
    alphabet,
    index
  );
  logD("Alphabet size %d", alphabet.size());

  for (int i = 0; i < 2; i++) {
    std::vector<AlphaChar> alphaSequence;
    translateString(
      channelSequences[i],
      representativeMap,
      index,
      alphaSequence
    );
    logD("Sequence length %d", alphaSequence.size());

    SuffixTree *root = createSuffixTree(
      alphabet,
      alphaSequence
    );

    // maximal common substring
    SuffixTree *maximal = root->find_maximal_substring();
    logD("maximal substring: %d (%d, %d)", maximal->depth, maximal->start, maximal->depth);
    testCompress(root, alphaSequence);

    delete root;

  }

  // BUGBUG: Not production 
  // just testing 
  testCommonSubsequences("banana");
  testCommonSubsequences("xabcyiiizabcqabcyr");
  testCommonSubsequencesBrute("banana");
  testCommonSubsequencesBrute("xabcyiiizabcqabcyr");
  // findCommonSubSequences(
  //   channelSequences[0],
  //   commonDumpSequences,
  //   representativeMap
    // );


}

/**
 *  Write note data. Format:
 * 
 *   fffff010 wwwwvvvv           frequency + waveform + volume, duration 1
 *   fffff100 wwwwvvvv           " " ", duration 2
 *   fffff110 dddddddd wwwwvvvv  " " ", duration d
 *   xxxx0001                    volume = x >> 4, duration 1 
 *   xxxx1001                    volume = x >> 4, duration 2
 *   xxxx0101                    wave = x >> 4, duration 1
 *   xxxx1101                    wave = x >> 4, duration 2
 *   xxxxx011                    frequency = x >> 3, duration 1
 *   xxxxx111                    frequency = x >> 3, duration 2
 *   00000000                    stop
 */
size_t DivExportAtari2600::writeNote(SafeWriter* w, const ChannelState& next, const char duration, const ChannelState& last) {
  size_t bytesWritten = 0;
  unsigned char dmod = 0; // if duration is small, store in top bits of frequency

  if (duration == 0) {
      logD("0 duration note");
  }
  const char framecount = duration > 0 ? duration - 1 : 0; // BUGBUG: when duration is zero... we force to 1...

  unsigned char audfx, audcx, audvx;
  int cc, fc, vc;
  audcx = next.registers[0];
  cc = audcx != last.registers[0];
  audfx = next.registers[1];
  fc = audfx != last.registers[1];
  audvx = next.registers[2];
  vc = audvx != last.registers[2];
  
  w->writeText(fmt::sprintf("    ;F%d C%d V%d D%d\n", audfx, audcx, audvx, duration));

  if ( ((cc + fc + vc) == 1) && framecount < 2) {
    // write a delta row - only change one register
    dmod = framecount; 
    unsigned char rx;
    if (fc > 0) {
      // frequency
      rx = audfx << 3 | dmod << 2 | 0x03; //  d11
    } else if (cc > 0 ) {
      // waveform
      rx = audcx << 3 | dmod << 3 | 0x05; // d101
    } else {
      // volume 
      rx = audvx << 3 | dmod << 3 | 0x01; // d001
    }
    w->writeText(fmt::sprintf("    byte %d\n", rx));
    bytesWritten += 1;

  } else {
    // write all registers
    if (framecount < 2) {
      // short duration
      dmod = framecount << 1 | 0x01; // BUGBUG: complicated format
    } else {
      dmod = 0x02;
    }
    // frequency
    unsigned char x = audfx << 3 | dmod << 1;
    w->writeText(fmt::sprintf("    byte %d", x));
    if (dmod == 0x02) {
      w->writeText(fmt::sprintf(",%d", framecount));
      bytesWritten += 1;
    }
    // waveform and volume
    unsigned char y = (audcx << 4) + audvx;
    w->writeText(fmt::sprintf(",%d\n", y));
    bytesWritten += 2;

  }

  return bytesWritten;

}

void DivExportAtari2600::writeNoteBinary(SafeWriter* w, const ChannelState& next, const char duration, const ChannelState& last) {
  unsigned char dmod = 0; // if duration is small, store in top bits of frequency

  unsigned char audfx, audcx, audvx;
  int cc, fc, vc;
  audcx = next.registers[0];
  cc = audcx != last.registers[0];
  audfx = next.registers[1];
  fc = audfx != last.registers[1];
  audvx = next.registers[2];
  vc = audvx != last.registers[2];
  

  if ( ((cc + fc + vc) == 1) && duration < 3) {
    // write a delta row - only change one register
    if (duration == 0) {
        logD("0 duration note");
    }
    dmod = duration > 0 ? duration - 1 : 1; // BUGBUG: when duration is zero... we force to 1...
    unsigned char rx;
    if (fc > 0) {
      // frequency
      rx = audfx << 3 | dmod << 2 | 0x03; //  d11
    } else if (cc > 0 ) {
      // waveform
      rx = audcx << 3 | dmod << 3 | 0x05; // d101
    } else {
      // volume 
      rx = audvx << 3 | dmod << 3 | 0x01; // d001
    }
    w->writeC(rx);

  } else {
    // write all registers
    if (duration < 3) {
      // short duration
      dmod = duration;
    } else {
      dmod = 3;
    }
    // frequency
    unsigned char x = audfx << 3 | dmod << 1;
    w->writeC(x);
    if (dmod == 3) {
      w->writeC(duration);
    }
    // waveform and volume
    unsigned char y = (audcx << 4) + audvx;
    w->writeC(y);

  }
}

void DivExportAtari2600::writeWaveformHeader(SafeWriter* w, const char * key) {
  w->writeText(fmt::sprintf("%s_ADDR\n", key));
}


int getFontIndex(const char c) {
  if ('0' <= c && c <= '9') return c - '0';
  if (c == ' ' || c == 0) return 10;
  if (c == '.') return 12;
  if (c == '<') return 13;
  if (c == '>') return 14;
  if ('a' <= c && c <= 'z') return 15 + c - 'a';
  if ('A' <= c && c <= 'Z') return 15 + c - 'A';
  return 11;
}

// 4x6 font data used to encode title
unsigned char FONT_DATA[41][6] = {
  {0x00, 0x04, 0x0a, 0x0a, 0x0a, 0x04}, // SYMBOL_ZERO
  {0x00, 0x0e, 0x04, 0x04, 0x04, 0x0c}, // SYMBOL_ONE
  {0x00, 0x0e, 0x08, 0x06, 0x02, 0x0c}, // SYMBOL_TWO
  {0x00, 0x0c, 0x02, 0x06, 0x02, 0x0c}, // SYMBOL_THREE
  {0x00, 0x02, 0x02, 0x0e, 0x0a, 0x0a}, // SYMBOL_FOUR
  {0x00, 0x0c, 0x02, 0x0c, 0x08, 0x06}, // SYMBOL_FIVE
  {0x00, 0x06, 0x0a, 0x0c, 0x08, 0x06}, // SYMBOL_SIX
  {0x00, 0x08, 0x08, 0x04, 0x02, 0x0e}, // SYMBOL_SEVEN
  {0x00, 0x06, 0x0a, 0x0e, 0x0a, 0x0c}, // SYMBOL_EIGHT
  {0x00, 0x02, 0x02, 0x0e, 0x0a, 0x0c}, // SYMBOL_NINE
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // SYMBOL_SPACE
  {0x00, 0x0e, 0x00, 0x00, 0x00, 0x00}, // SYMBOL_UNDERSCORE
  {0x00, 0x04, 0x00, 0x00, 0x00, 0x00}, // SYMBOL_DOT
  {0x00, 0x02, 0x04, 0x08, 0x04, 0x02}, // SYMBOL_LT
  {0x00, 0x08, 0x04, 0x02, 0x04, 0x08}, // SYMBOL_GT
  {0x00, 0x0a, 0x0a, 0x0e, 0x0a, 0x0e}, // SYMBOL_A
  {0x00, 0x0e, 0x0a, 0x0c, 0x0a, 0x0e}, // SYMBOL_B
  {0x00, 0x0e, 0x08, 0x08, 0x08, 0x0e}, // SYMBOL_C
  {0x00, 0x0c, 0x0a, 0x0a, 0x0a, 0x0c}, // SYMBOL_D
  {0x00, 0x0e, 0x08, 0x0c, 0x08, 0x0e}, // SYMBOL_E
  {0x00, 0x08, 0x08, 0x0c, 0x08, 0x0e}, // SYMBOL_F
  {0x00, 0x0e, 0x0a, 0x08, 0x08, 0x0e}, // SYMBOL_G
  {0x00, 0x0a, 0x0a, 0x0e, 0x0a, 0x0a}, // SYMBOL_H
  {0x00, 0x04, 0x04, 0x04, 0x04, 0x04}, // SYMBOL_I
  {0x00, 0x0e, 0x0a, 0x02, 0x02, 0x02}, // SYMBOL_J
  {0x00, 0x0a, 0x0a, 0x0c, 0x0a, 0x0a}, // SYMBOL_K
  {0x00, 0x0e, 0x08, 0x08, 0x08, 0x08}, // SYMBOL_L
  {0x00, 0x0a, 0x0a, 0x0e, 0x0e, 0x0e}, // SYMBOL_M
  {0x00, 0x0a, 0x0a, 0x0a, 0x0a, 0x0e}, // SYMBOL_N
  {0x00, 0x0e, 0x0a, 0x0a, 0x0a, 0x0e}, // SYMBOL_O
  {0x00, 0x08, 0x08, 0x0e, 0x0a, 0x0e}, // SYMBOL_P
  {0x00, 0x06, 0x08, 0x0a, 0x0a, 0x0e}, // SYMBOL_Q
  {0x00, 0x0a, 0x0a, 0x0c, 0x0a, 0x0e}, // SYMBOL_R
  {0x00, 0x0e, 0x02, 0x0e, 0x08, 0x0e}, // SYMBOL_S
  {0x00, 0x04, 0x04, 0x04, 0x04, 0x0e}, // SYMBOL_T
  {0x00, 0x0e, 0x0a, 0x0a, 0x0a, 0x0a}, // SYMBOL_U
  {0x00, 0x04, 0x04, 0x0e, 0x0a, 0x0a}, // SYMBOL_V
  {0x00, 0x0e, 0x0e, 0x0e, 0x0a, 0x0a}, // SYMBOL_W
  {0x00, 0x0a, 0x0e, 0x04, 0x0e, 0x0a}, // SYMBOL_X
  {0x00, 0x04, 0x04, 0x0e, 0x0a, 0x0a}, // SYMBOL_Y
  {0x00, 0x0e, 0x08, 0x04, 0x02, 0x0e}  // SYMBOL_Z
};

size_t DivExportAtari2600::writeTextGraphics(SafeWriter* w, const char* value) {
  size_t bytesWritten = 0;

  bool end = false;
  size_t len = 0; 
  while (len < 6 || !end) {
    w->writeText(fmt::sprintf("TITLE_GRAPHICS_%d\n    byte ", len));
    len++;
    char ax = 0;
    if (!end) {
      ax = *value++;
      if (0 == ax) {
        end = true;
      }
    } 
    char bx = 0;
    if (!end) {
      bx = *value++;
      if (0 == bx) end = true;
    }
    auto ai = getFontIndex(ax);
    auto bi = getFontIndex(bx);
      for (int i = 0; i < 6; i++) {
      if (i > 0) {
        w->writeText(",");
      }
      const unsigned char c = (FONT_DATA[ai][i] << 4) + FONT_DATA[bi][i];
      w->writeText(fmt::sprintf("%d", c));
      bytesWritten += 1;
    }
    w->writeText("\n");
  }
  w->writeText(fmt::sprintf("TITLE_LENGTH = %d", len));
  return bytesWritten;
}
