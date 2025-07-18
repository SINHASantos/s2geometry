// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS-IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// Author: ericv@google.com (Eric Veach)

#include "s2/id_set_lexicon.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "absl/log/absl_check.h"
#include "s2/sequence_lexicon.h"

using std::vector;

IdSetLexicon::IdSetLexicon() = default;

IdSetLexicon::~IdSetLexicon() = default;

// We define the copy/move constructors and assignment operators explicitly
// in order to avoid copying/moving the temporary storage vector "tmp_".

IdSetLexicon::IdSetLexicon(const IdSetLexicon& x) : id_sets_(x.id_sets_) {
}

IdSetLexicon::IdSetLexicon(IdSetLexicon&& x) noexcept
    : id_sets_(std::move(x.id_sets_)) {}

IdSetLexicon& IdSetLexicon::operator=(const IdSetLexicon& x) {
  id_sets_ = x.id_sets_;
  return *this;
}

IdSetLexicon& IdSetLexicon::operator=(IdSetLexicon&& x) noexcept {
  id_sets_ = std::move(x.id_sets_);
  return *this;
}

void IdSetLexicon::Clear() {
  id_sets_.Clear();
}

int32_t IdSetLexicon::AddInternal(vector<int32_t>* ids) {
  if (ids->empty()) {
    // Empty sets have a special id chosen not to conflict with other ids.
    return kEmptySetId;
  } else if (ids->size() == 1) {
    // Singleton sets are represented by their element.
    return (*ids)[0];
  } else {
    // Canonicalize the set by sorting and removing duplicates.
    std::sort(ids->begin(), ids->end());
    ids->erase(std::unique(ids->begin(), ids->end()), ids->end());

    // After eliminating duplicates, we may now have a singleton.
    if (ids->size() == 1) return (*ids)[0];

    // Non-singleton sets are represented by the bitwise complement of the id
    // returned by SequenceLexicon.
    return ~id_sets_.Add(*ids);
  }
}

IdSetLexicon::IdSet IdSetLexicon::id_set(int32_t set_id) const {
  if (set_id >= 0) {
    return IdSet(set_id);
  } else if (set_id == kEmptySetId) {
    return IdSet();
  } else {
    auto sequence = id_sets_.sequence(~set_id);
    ABSL_DCHECK_NE(0, sequence.size());
    return IdSet(&*sequence.begin(), &*sequence.begin() + sequence.size());
  }
}
