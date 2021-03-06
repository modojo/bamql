/*
 * Copyright 2015 Paul Boutros. For details, see COPYING. Our lawyer cats sez:
 *
 * OICR makes no representations whatsoever as to the SOFTWARE contained
 * herein.  It is experimental in nature and is provided WITHOUT WARRANTY OF
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE OR ANY OTHER WARRANTY,
 * EXPRESS OR IMPLIED. OICR MAKES NO REPRESENTATION OR WARRANTY THAT THE USE OF
 * THIS SOFTWARE WILL NOT INFRINGE ANY PATENT OR OTHER PROPRIETARY RIGHT.  By
 * downloading this SOFTWARE, your Institution hereby indemnifies OICR against
 * any loss, claim, damage or liability, of whatsoever kind or nature, which
 * may arise from your Institution's respective use, handling or storage of the
 * SOFTWARE. If publications result from research using this SOFTWARE, we ask
 * that the Ontario Institute for Cancer Research be acknowledged and/or
 * credit be given to OICR scientists, as scientifically appropriate.
 */

#include <cstdio>
#include <climits>
#include <iostream>
#include <sstream>
#include "bamql-iterator.hpp"

bamql::ReadIterator::ReadIterator() {}

static bool checkHtsError(int result) {
  if (result == -1) {
    /* No error. */
    return true;
  } else if (result == -2) {
    std::cerr << "Input file is truncated." << std::endl;
  } else if (result == -3) {
    std::cerr << "Record corrupt." << std::endl;
  } else if (result == -4) {
    std::cerr << "Out of memory." << std::endl;
  } else {
    std::cerr << "A mysterious error occurred: " << result << std::endl;
  }
  return false;
}

bool bamql::ReadIterator::wantAll(std::shared_ptr<bam_hdr_t> &header) {
  for (auto tid = 0; tid < header->n_targets; tid++) {
    if (!wantChromosome(header, tid)) {
      return false;
    }
  }
  return true;
}
bool bamql::ReadIterator::processFile(const char *file_name,
                                      bool binary,
                                      bool ignore_index) {
  // Open the input file.
  auto input = bamql::open(file_name, binary ? "rb" : "r");
  if (!input) {
    perror(file_name);
    return false;
  }

  // Copy the header to the output.
  std::shared_ptr<bam_hdr_t> header(sam_hdr_read(input.get()), bam_hdr_destroy);
  ingestHeader(header);

  // Open the index, if desired.
  std::shared_ptr<hts_idx_t> index(
      ignore_index ? nullptr : hts_idx_load(file_name, HTS_FMT_BAI),
      hts_idx_destroy);

  if (index && !wantAll(header)) {
    std::shared_ptr<bam1_t> read(bam_init1(), bam_destroy1);
    // Rummage through all the chromosomes in the header...
    for (auto tid = 0; tid < header->n_targets; tid++) {
      if (!wantChromosome(header, tid)) {
        continue;
      }
      // ...and use the index to seek through chomosome of interest.
      std::shared_ptr<hts_itr_t> itr(
          bam_itr_queryi(index.get(), tid, 0, INT_MAX), hts_itr_destroy);
      int result;
      while ((result = bam_itr_next(input.get(), itr.get(), read.get())) >= 0) {
        processRead(header, read);
      }
      if (!checkHtsError(result)) {
        return false;
      }
    }
    return true;
  }

  // Cycle through all the reads when an index is unavailable.
  std::shared_ptr<bam1_t> read(bam_init1(), bam_destroy1);
  int result;
  while ((result = sam_read1(input.get(), header.get(), read.get())) >= 0) {
    processRead(header, read);
  }
  return checkHtsError(result);
}
