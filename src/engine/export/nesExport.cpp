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

#include "nesExport.h"
#include "registerDump.h"

#include <fmt/printf.h>
#include <set>
#include "../../ta-log.h"

const unsigned int S0Volume = 0x4000;
const unsigned int S0Sweep = 0x4001;
const unsigned int S0PeriodL = 0x4002;
const unsigned int S0PeriodH = 0x4003;
const unsigned int S1Volume = 0x4004;
const unsigned int S1Sweep = 0x4005;
const unsigned int S1PeriodL = 0x4006;
const unsigned int S1PeriodH = 0x4007;
const unsigned int TRVolume = 0x4008;
const unsigned int TRPeriodL = 0x400A;
const unsigned int TRPeriodH = 0x400B;
const unsigned int NSVolume = 0x400C;
const unsigned int NSPeriod = 0x400E;
const unsigned int NSLength = 0x400F;
const unsigned int DMCControl = 0x4010;
const unsigned int DMCLoad = 0x4011;
const unsigned int DMCAddr = 0x4012;
const unsigned int DMCLength = 0x4013;
const unsigned int APUControl = 0x4015;
const unsigned int APUFrameCtl = 0x4017;


std::map<unsigned int, unsigned int> nesVoice0AddressMap = {
  {S0Volume, 0},
  {S0Sweep, 1},
  {S0PeriodL, 2},
  {S0PeriodH, 3}
};

std::map<unsigned int, unsigned int> nesVoice1AddressMap = {
  {S1Volume, 0},
  {S1Sweep, 1},
  {S1PeriodL, 2},
  {S1PeriodH, 3}
};

std::map<unsigned int, unsigned int> nesTriangleAddressMap = {
  {TRVolume, 0},
  {TRPeriodL, 2},
  {TRPeriodH, 3}
};

std::map<unsigned int, unsigned int> nesNoiseAddressMap = {
  {NSVolume, 0},
  {NSPeriod, 2},
  {NSLength, 3}
};

std::map<unsigned int, unsigned int> nesDMCAddressMap = {
  {DMCControl, 0},
  {DMCLoad, 1},
  {DMCAddr, 2},
  {DMCLength, 3}
};

std::map<unsigned int, unsigned int> nesAPUAddressMap = {
  {APUControl, 0},
  {APUFrameCtl, 1}
};

struct NESVoiceRegisters  {

  unsigned char volume;
  unsigned char sweep;   // unused for triangle and square wave
  unsigned char periodH;
  unsigned char periodL;

  NESVoiceRegisters() {}

  NESVoiceRegisters(unsigned char c) : volume(c), sweep(c), periodH(c), periodL(c) {}

  bool write(const unsigned int addr, const unsigned int value) {
    unsigned char val = value;
    switch (addr) {
      case 0:
        if (val == volume) return false;
        volume = val;
        return true;
      case 1:
        if (val == sweep) return false;
        sweep = val;
        return true;
      case 2:
        if (val == periodH) return false;
        periodH = val;
        return true;
      case 3:
        if (val == periodL) return false;
        periodL = val;
        return true;
    }
    return false;
  }

  uint64_t hash_interval(const char duration) {
    return ((uint64_t)volume) +
           (((uint64_t)sweep) << 8) +
           (((uint64_t)periodH) << 16) + 
           (((uint64_t)periodL) << 24) + 
           (((uint64_t)duration) << 32);
  }

};

struct NESDMCRegisters  {

  unsigned char control;
  unsigned char load;   // unused
  unsigned char addr;
  unsigned char length;

  NESDMCRegisters() {}

  NESDMCRegisters(unsigned char c) : control(c), load(c), addr(c), length(c) {}

  bool write(const unsigned int address, const unsigned int value) {
    unsigned char val = value;
    switch (address) {
      case 0:
        if (val == control) return false;
        control = val;
        return true;
      case 1:
        if (val == load) return false;
        load = val;
        return true;
      case 2:
        if (val == addr) return false;
        addr = val;
        return true;
      case 3:
        if (val == length) return false;
        length = val;
        return true;
    }
    return false;
  }

  uint64_t hash_interval(const char duration) {
    return ((uint64_t)control) +
           (((uint64_t)load) << 8) +
           (((uint64_t)addr) << 16) + 
           (((uint64_t)length) << 24) + 
           (((uint64_t)duration) << 32);
  }

};

struct NESAPURegisters  {

  unsigned char control;
  unsigned char frameCtl;

  NESAPURegisters() {}

  NESAPURegisters(unsigned char c) : control(c), frameCtl(c) {}

  bool write(const unsigned int address, const unsigned int value) {
    unsigned char val = value;
    switch (address) {
      case 0:
        if (val == control) return false;
        control = val;
        return true;
      case 1:
        if (val == frameCtl) return false;
        frameCtl = val;
        return true;
    }
    return false;
  }

  uint64_t hash_interval(const char duration) {
    return ((uint64_t)control) +
           (((uint64_t)frameCtl) << 8) +
           (((uint64_t)duration) << 16);
  }

};

std::vector<DivROMExportOutput> DivExportNES::go(DivEngine* e) {

  DivSystem targetSystem = DIV_SYSTEM_NES;

  // capture all sequences
  logD("performing sequence capture");
  std::map<String, DumpSequence<NESVoiceRegisters>> voiceSequences;
  std::map<String, DumpSequence<NESVoiceRegisters>> triangleSequences;
  std::map<String, DumpSequence<NESVoiceRegisters>> noiseSequences;
  std::map<String, DumpSequence<NESDMCRegisters>> dmcSequences;
  std::map<String, DumpSequence<NESAPURegisters>> apuSequences;
  captureSequences(e, targetSystem, 0, nesVoice0AddressMap, voiceSequences);
  captureSequences(e, targetSystem, 1, nesVoice1AddressMap, voiceSequences);
  captureSequences(e, targetSystem, 2, nesTriangleAddressMap, triangleSequences);
  captureSequences(e, targetSystem, 3, nesNoiseAddressMap, noiseSequences);
  captureSequences(e, targetSystem, 4, nesDMCAddressMap, dmcSequences);
  captureSequences(e, targetSystem, 5, nesAPUAddressMap, apuSequences);

  logD("found %d voice sequences", voiceSequences.size());
  logD("found %d triangle sequences", triangleSequences.size());
  logD("found %d noise sequences", noiseSequences.size());
  logD("found %d dmc sequences", dmcSequences.size());
  logD("found %d apu sequences", apuSequences.size());

  // sequence frequency stats
  std::map<uint64_t, unsigned int> sequenceFrequency;
  // sequence lookup 
  std::map<String, String> representativeSequenceMap;

  // compress the voices into common subsequences
  logD("performing voice sequence compression");
  std::map<uint64_t, String> commonVoiceSubSequences;
  findCommonSubsequences(
    voiceSequences,
    commonVoiceSubSequences,
    sequenceFrequency,
    representativeSequenceMap);
  logD("found %d common voice sequences", commonVoiceSubSequences.size());

  // compress the triangle registers into common subsequences
  logD("performing triangle sequence compression");
  std::map<uint64_t, String> commonTriangleSubSequences;
  findCommonSubsequences(
    triangleSequences,
    commonTriangleSubSequences,
    sequenceFrequency,
    representativeSequenceMap);
  logD("found %d common triangle sequences", commonTriangleSubSequences.size());

  // compress the noise registers into common subsequences
  logD("performing noise sequence compression");
  std::map<uint64_t, String> commonNoiseSubSequences;
  findCommonSubsequences(
    noiseSequences,
    commonNoiseSubSequences,
    sequenceFrequency,
    representativeSequenceMap);
  logD("found %d common triangle sequences", commonNoiseSubSequences.size());

  std::vector<DivROMExportOutput> ret;
  ret.reserve(1);

  // create track data
  logD("writing track audio data");
  SafeWriter* w = new SafeWriter;
  w->init();

  w->writeText(fmt::sprintf("; Song: %s\n", e->song.name));
  w->writeText(fmt::sprintf("; Author: %s\n", e->song.author));

  // emit song table
  logD("writing song table");
  size_t songTableSize = 0;
  w->writeText("\n; Song Lookup Table\n");
  w->writeText(fmt::sprintf("NUM_SONGS = %d\n", e->song.subsong.size()));
  w->writeText("SONG_TABLE_START_LO\n");
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    w->writeText(fmt::sprintf("SONG_%d = . - SONG_TABLE_START_LO\n", i));
    w->writeText(fmt::sprintf("    byte <SONG_%d_ADDR\n", i));
    songTableSize++;
  }
  w->writeText("SONG_TABLE_START_HI\n");
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    w->writeText(fmt::sprintf("    byte >SONG_%d_ADDR\n", i));
    songTableSize++;
  }

  // collect and emit song data
  // borrowed from fileops
  size_t songDataSize = 0;
  w->writeText("; songs\n");
  std::vector<PatternIndex> patterns;
  bool alreadyAdded[2][256];
  for (size_t i = 0; i < e->song.subsong.size(); i++) {
    w->writeText(fmt::sprintf("SONG_%d_ADDR\n", i));
    DivSubSong* subs = e->song.subsong[i];
    memset(alreadyAdded, 0, 2*256*sizeof(bool));
    for (int j = 0; j < subs->ordersLen; j++) {
      w->writeText("    byte ");
      for (int k = 0; k < e->getChannelCount(DIV_SYSTEM_C64_6581); k++) {
        if (k > 0) {
          w->writeText(", ");
        }
        unsigned short p = subs->orders.ord[k][j];
        logD("ss: %d ord: %d chan: %d pat: %d", i, j, k, p);
        String key = getPatternKey(i, k, p);
        w->writeText(key);
        songDataSize++;
        if (alreadyAdded[k][p]) continue;
        patterns.push_back(PatternIndex(key, i, j, k, p));
        alreadyAdded[k][p] = true;
      }
      w->writeText("\n");
    }
    w->writeText("    byte 255\n");
    songDataSize++;
  }

  // pattern lookup
  size_t patternTableSize = 0;
  w->writeC('\n');
  w->writeText("; Pattern Lookup Table\n");
  w->writeText(fmt::sprintf("NUM_PATTERNS = %d\n", patterns.size()));
  w->writeText("PAT_TABLE_START_LO\n");
  for (PatternIndex& patternIndex: patterns) {
    w->writeText(fmt::sprintf("%s = . - PAT_TABLE_START_LO\n", patternIndex.key.c_str()));
    w->writeText(fmt::sprintf("   byte <%s_ADDR\n", patternIndex.key.c_str()));
    patternTableSize++;
  }
  w->writeText("PAT_TABLE_START_HI\n");
  for (PatternIndex& patternIndex: patterns) {
    w->writeText(fmt::sprintf("   byte >%s_ADDR\n", patternIndex.key.c_str()));
    patternTableSize++;
  }

  // emit sequences
  // we emit the "note" being played as an assembly variable 
  // later we will figure out what we need to emit as far as TIA register settings
  // this assumes the song has a limited number of unique "notes"
  size_t patternDataSize = 0;
  for (PatternIndex& patternIndex: patterns) {
    DivPattern* pat = e->song.subsong[patternIndex.subsong]->pat[patternIndex.chan].getPattern(patternIndex.pat, false);
    w->writeText(fmt::sprintf("; Subsong: %d Channel: %d Pattern: %d / %s\n", patternIndex.subsong, patternIndex.chan, patternIndex.pat, pat->name));
    w->writeText(fmt::sprintf("%s_ADDR", patternIndex.key.c_str()));
    for (int j = 0; j<e->song.subsong[patternIndex.subsong]->patLen; j++) {
      if (j % 8 == 0) {
        w->writeText("\n    byte ");
      } else {
        w->writeText(",");
      }
      String key = getSequenceKey(patternIndex.subsong, patternIndex.ord, j, patternIndex.chan);
      w->writeText(representativeSequenceMap[key]); // the representative
      patternDataSize++;
    }
    w->writeText("\n    byte 255\n");
    patternDataSize++;
  }

  // emit voice waveform table
  size_t voiceWaveformTableSize = 0;
  w->writeC('\n');
  w->writeText("; Voice Waveform Lookup Table\n");
  w->writeText(fmt::sprintf("NUM_VOICE_WAVEFORMS = %d\n", commonVoiceSubSequences.size()));
  w->writeText("WF_VOICE_TABLE_START_LO\n");
  for (auto& x: commonVoiceSubSequences) {
    w->writeText(fmt::sprintf("%s = . - WF_VOICE_TABLE_START_LO\n", x.second.c_str()));
    w->writeText(fmt::sprintf("   byte <%s_ADDR\n", x.second.c_str()));
    voiceWaveformTableSize++;
  }
  w->writeText("WF_VOICE_TABLE_START_HI\n");
  for (auto& x: commonVoiceSubSequences) {
    w->writeText(fmt::sprintf("   byte >%s_ADDR\n", x.second.c_str()));
    voiceWaveformTableSize++;
  }

  // emit triangle waveform table
  size_t triangleWaveformTableSize = 0;
  w->writeC('\n');
  w->writeText("; Triangle Waveform Lookup Table\n");
  w->writeText(fmt::sprintf("NUM_FILTER_WAVEFORMS = %d\n", commonTriangleSubSequences.size()));
  w->writeText("WF_TRIANGLE_TABLE_START_LO\n");
  for (auto& x: commonTriangleSubSequences) {
    w->writeText(fmt::sprintf("%s = . - WF_TRIANGLE_TABLE_START_LO\n", x.second.c_str()));
    w->writeText(fmt::sprintf("   byte <%s_ADDR\n", x.second.c_str()));
    triangleWaveformTableSize++;
  }
  w->writeText("WF_TRIANGLE_TABLE_START_HI\n");
  for (auto& x: commonTriangleSubSequences) {
    w->writeText(fmt::sprintf("   byte >%s_ADDR\n", x.second.c_str()));
    triangleWaveformTableSize++;
  }

  // emit noise waveform table
  size_t noiseWaveformTableSize = 0;
  w->writeC('\n');
  w->writeText("; Noise Waveform Lookup Table\n");
  w->writeText(fmt::sprintf("NUM_FILTER_WAVEFORMS = %d\n", commonNoiseSubSequences.size()));
  w->writeText("WF_NOISE_TABLE_START_LO\n");
  for (auto& x: commonNoiseSubSequences) {
    w->writeText(fmt::sprintf("%s = . - WF_NOISE_TABLE_START_LO\n", x.second.c_str()));
    w->writeText(fmt::sprintf("   byte <%s_ADDR\n", x.second.c_str()));
    noiseWaveformTableSize++;
  }
  w->writeText("WF_NOISE_TABLE_START_HI\n");
  for (auto& x: commonNoiseSubSequences) {
    w->writeText(fmt::sprintf("   byte >%s_ADDR\n", x.second.c_str()));
    noiseWaveformTableSize++;
  }

  // emit voice waveforms
  size_t voiceWaveformDataSize = 0;
  w->writeC('\n');
  w->writeText("; Voice Waveforms\n");
  for (auto& x: commonVoiceSubSequences) {
    auto freq = sequenceFrequency[x.first];
    w->writeText(fmt::sprintf("%s_ADDR\n", x.second.c_str()));
    w->writeText(fmt::sprintf("; Hash %d, Freq %d\n", x.first, freq));
    auto& dump = voiceSequences[x.second];
    for (auto& n: dump.intervals) {
      w->writeText(
        fmt::sprintf(
          "    byte %d,%d,%d,%d,%d\n",
          n.duration,
          n.state.volume,
          n.state.sweep,
          n.state.periodH,
          n.state.periodL
        )
      );
      voiceWaveformDataSize += 5;
    }
    w->writeText("    byte 255\n");
    voiceWaveformDataSize++;
  }

  // emit triangle waveforms
  size_t triangleWaveformDataSize = 0;
  w->writeC('\n');
  w->writeText("; Triangle Waveforms\n");
  for (auto& x: commonTriangleSubSequences) {
    auto freq = sequenceFrequency[x.first];
    w->writeText(fmt::sprintf("%s_ADDR\n", x.second.c_str()));
    w->writeText(fmt::sprintf("; Hash %d, Freq %d\n", x.first, freq));
    auto& dump = triangleSequences[x.second];
    for (auto& n: dump.intervals) {
      w->writeText(
        fmt::sprintf(
          "    byte %d,%d,%d,%d\n",
          n.duration,
          n.state.volume,
          n.state.periodH,
          n.state.periodL
        )
      );
      triangleWaveformDataSize += 4;
    }
    w->writeText("    byte 255\n");
    triangleWaveformDataSize++;
  }

  // emit noise waveforms
  size_t noiseWaveformDataSize = 0;
  w->writeC('\n');
  w->writeText("; Noise Waveforms\n");
  for (auto& x: commonTriangleSubSequences) {
    auto freq = sequenceFrequency[x.first];
    w->writeText(fmt::sprintf("%s_ADDR\n", x.second.c_str()));
    w->writeText(fmt::sprintf("; Hash %d, Freq %d\n", x.first, freq));
    auto& dump = noiseSequences[x.second];
    for (auto& n: dump.intervals) {
      w->writeText(
        fmt::sprintf(
          "    byte %d,%d,%d,%d\n",
          n.duration,
          n.state.volume,
          n.state.periodH,
          n.state.periodL
        )
      );
      noiseWaveformDataSize += 4;
    }
    w->writeText("    byte 255\n");
    noiseWaveformDataSize++;
  }

  w->writeC('\n');
  // audio metadata
  w->writeC('\n');
  w->writeText(fmt::sprintf("; Song Table Size %d\n", songTableSize));
  w->writeText(fmt::sprintf("; Song Data Size %d\n", songDataSize));
  w->writeText(fmt::sprintf("; Pattern Lookup Table Size %d\n", patternTableSize));
  w->writeText(fmt::sprintf("; Pattern Data Size %d\n", patternDataSize));
  w->writeText(fmt::sprintf("; Voice Waveform Table Size %d\n", voiceWaveformTableSize));
  w->writeText(fmt::sprintf("; Triangle Waveform Table Size %d\n", triangleWaveformDataSize));
  w->writeText(fmt::sprintf("; Voice Waveform Data Size %d\n", voiceWaveformDataSize));
  w->writeText(fmt::sprintf("; Triangle Waveform Data Size %d\n", triangleWaveformDataSize));
  size_t totalDataSize = 
    songTableSize + songDataSize + patternTableSize + 
    patternDataSize + voiceWaveformTableSize + triangleWaveformTableSize +
    voiceWaveformDataSize + triangleWaveformDataSize;
  w->writeText(fmt::sprintf("; Total Data Size %d\n", totalDataSize));

  ret.push_back(DivROMExportOutput("Track_data.asm", w));

  return ret;

}
