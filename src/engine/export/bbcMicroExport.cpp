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

#include "bbcMicroExport.h"
#include "registerDump.h"

#include <fmt/printf.h>
#include <set>
#include "../../ta-log.h"

const unsigned int DATA = 0x00;

std::map<unsigned int, unsigned int> dataAddressMap = {
  {DATA, 0}
};

struct SN76489DataRegister {

  unsigned char data;

  SN76489DataRegister() {}

  SN76489DataRegister(unsigned char c) : data(c) {}

  bool write(const unsigned int addr, const unsigned int value) {
    unsigned char val = value;
    switch (addr) {
      case 0:
        if (val == data) return false;
        data = val;
        return true;
    }
    return false;
  }

  uint64_t hash_interval(const char duration) {
    return ((uint64_t)data) +
           (((uint64_t)duration) << 8);
  }

};


std::vector<DivROMExportOutput> DivExportBBCMicro::go(DivEngine* e) {

  DivSystem targetSystem = DIV_SYSTEM_SMS;

  // capture all sequences
  logD("performing sequence capture");
  std::map<String, DumpSequence<SN76489DataRegister>> dataSequences;
  captureSequences(e, targetSystem, 0, dataAddressMap, dataSequences);
  size_t complexData = 0;
  for (auto& x: dataSequences) {
    if (x.second.intervals.size() > 1) {
      complexData++;
    }
  }
  logD("found %d data sequences, %d are complex", dataSequences.size(), complexData);

  // sequence frequency stats
  std::map<uint64_t, unsigned int> sequenceFrequency;
  // sequence lookup 
  std::map<String, String> representativeSequenceMap;

  // compress the voices into common subsequences
  logD("performing voice sequence compression");
  std::map<uint64_t, String> commonDataSubSequences;
  findCommonSubsequences(
    dataSequences,
    commonDataSubSequences,
    sequenceFrequency,
    representativeSequenceMap);
  logD("found %d common voice sequences", commonDataSubSequences.size());

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
      for (int k = 0; k < e->getChannelCount(targetSystem); k++) {
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
  w->writeText(fmt::sprintf("NUM_VOICE_WAVEFORMS = %d\n", commonDataSubSequences.size()));
  w->writeText("WF_VOICE_TABLE_START_LO\n");
  for (auto& x: commonDataSubSequences) {
    w->writeText(fmt::sprintf("%s = . - WF_VOICE_TABLE_START_LO\n", x.second.c_str()));
    w->writeText(fmt::sprintf("   byte <%s_ADDR\n", x.second.c_str()));
    voiceWaveformTableSize++;
  }
  w->writeText("WF_VOICE_TABLE_START_HI\n");
  for (auto& x: commonDataSubSequences) {
    w->writeText(fmt::sprintf("   byte >%s_ADDR\n", x.second.c_str()));
    voiceWaveformTableSize++;
  }


  // emit voice waveforms
  size_t voiceWaveformDataSize = 0;
  w->writeC('\n');
  w->writeText("; Voice Waveforms\n");
  for (auto& x: commonDataSubSequences) {
    auto freq = sequenceFrequency[x.first];
    w->writeText(fmt::sprintf("%s_ADDR\n", x.second.c_str()));
    w->writeText(fmt::sprintf("; Hash %d, Freq %d\n", x.first, freq));
    auto& dump = dataSequences[x.second];
    for (auto& n: dump.intervals) {
      w->writeText(
        fmt::sprintf(
          "    byte %d,%d\n",
          n.duration,
          n.state.data
        )
      );
      voiceWaveformDataSize += 2;
    }
    w->writeText("    byte 255\n");
    voiceWaveformDataSize++;
  }

  w->writeC('\n');
  // audio metadata
  w->writeC('\n');
  w->writeText(fmt::sprintf("; Song Table Size %d\n", songTableSize));
  w->writeText(fmt::sprintf("; Song Data Size %d\n", songDataSize));
  w->writeText(fmt::sprintf("; Pattern Lookup Table Size %d\n", patternTableSize));
  w->writeText(fmt::sprintf("; Pattern Data Size %d\n", patternDataSize));
  w->writeText(fmt::sprintf("; Voice Waveform Table Size %d\n", voiceWaveformTableSize));
  w->writeText(fmt::sprintf("; Voice Waveform Data Size %d\n", voiceWaveformDataSize));
  size_t totalDataSize = 
    songTableSize + songDataSize + patternTableSize + 
    patternDataSize + voiceWaveformTableSize +
    voiceWaveformDataSize;
  w->writeText(fmt::sprintf("; Total Data Size %d\n", totalDataSize));

  ret.push_back(DivROMExportOutput("Track_data.asm", w));

  return ret;

}
