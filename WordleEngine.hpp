#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"
#include "emp/bits/BitSet.hpp"
#include "emp/bits/BitVector.hpp"
#include "emp/datastructs/map_utils.hpp"
#include "emp/datastructs/vector_utils.hpp"
#include "emp/tools/string_utils.hpp"
#include "emp/debug/alert.hpp"

#include "Result.hpp"

using IDList = emp::vector<uint16_t>;

class IDSet {
private:
  emp::BitVector bit_ids;

public:
  IDSet() { }
  IDSet(size_t count) : bit_ids(count) { bit_ids.SetAll(); }
  IDSet(const IDList & _ids, int start_id, int end_id, size_t count)
    : bit_ids(count)
  {
    for (int i = start_id; i < end_id; ++i) bit_ids.Set(_ids[i]);
  }
  IDSet(const IDSet &) = default;
  IDSet(IDSet &&) = default;

  IDSet & operator=(const IDSet &) = default;
  IDSet & operator=(IDSet &&) = default;

  size_t size() const { return bit_ids.CountOnes(); }
  size_t GetSize() const { return size(); }

  void Resize(size_t count) { bit_ids.Resize(count); }

  IDList GetSorted() const {
    IDList out_ids;
    bit_ids.GetOnes(out_ids);
    return out_ids;
  }

  template <typename T>
  IDList GetSorted(T fun_less) const {
    IDList out_ids;
    bit_ids.GetOnes(out_ids);
    std::sort(out_ids.begin(), out_ids.end(), fun_less);
    return out_ids;
  }

  uint16_t operator[](size_t pos) const { return bit_ids[pos]; }


  void Clear() { bit_ids.resize(0); }
  IDList AsList() const { return GetSorted(); }

  uint16_t GetFirstID() const { return (uint16_t) bit_ids.FindOne(); }
  uint16_t GetNextID(size_t cur_id) const { return (uint16_t) bit_ids.FindOne(cur_id+1); }

  void Copy(const IDList & in_ids,
            size_t start_id,
            size_t end_id,
            size_t count
  ) {
    bit_ids.resize(count);
    bit_ids.Clear();
    for (size_t i=start_id; i < end_id; ++i) bit_ids.Set(in_ids[i]);
  }

  void Add(uint16_t id) { bit_ids.Set(id); }

  void SetAll(size_t count) {
    bit_ids.resize(count);
    bit_ids.SetAll();
  }

  // Intersection
  IDSet & operator&=(const IDSet & in) {
    bit_ids &= in.bit_ids;
    return *this;
  }

  // Removal
  IDSet & operator-=(IDSet & in) {
    bit_ids &= ~in.bit_ids;
    return *this;
  }
};

struct GroupStats {
  size_t max_options = 0;
  double ave_options = 0.0;
  double entropy = 0.0;
  double solve_p = 0.0;
};

struct IDGroups {
  IDList ids;
  IDList starts;
  uint16_t num_ids = 0;

  IDGroups() { }
  IDGroups(const IDGroups &) = default;
  IDGroups(IDGroups &&) = default;

  IDGroups & operator=(const IDGroups &) = default;
  IDGroups & operator=(IDGroups &&) = default;

  void Reset(size_t in_size) {
    ids.resize(in_size);
    starts.resize(0);
    num_ids = 0;
  }

  void AddGroup(const IDSet & new_group) {
    starts.push_back(num_ids);
    for (int id = new_group.GetFirstID(); id != (uint16_t) -1; id = new_group.GetNextID(id)) {
      ids[num_ids++] = static_cast<uint16_t>(id);
    }
  }

  IDSet GetGroup(size_t group_id) const {
    emp_assert(group_id < starts.size(), group_id, starts.size());
    int start_id = starts[group_id];
    int end_id = (group_id+1 < starts.size()) ? starts[group_id+1] : static_cast<int>(ids.size());
    return IDSet(ids, start_id, end_id, num_ids);
  }

  /// A faster (?) GetGroup that does not require a new allocation.
  void GetGroup(IDSet & out_set, size_t group_id) const {
    int start_id = starts[group_id];
    int end_id = (group_id+1 < starts.size()) ? starts[group_id+1] : static_cast<int>(ids.size());
    return out_set.Copy(ids, start_id, end_id, num_ids);
  }

  GroupStats CalcStats() {
    GroupStats out_stats;
    double total = 0.0;
    size_t solve_count = 0; // How many options narrow to a single word?
    for (size_t start_id = 0; start_id < starts.size(); ++start_id) {
      const size_t start_pos = starts[start_id];
      const size_t end_pos = (start_id+1 < starts.size()) ? starts[start_id+1] : ids.size();
      const size_t cur_size = end_pos - start_pos;
      if (cur_size == 1) ++solve_count;
      if (cur_size > out_stats.max_options) out_stats.max_options = cur_size;
      total += cur_size * cur_size;
      double p = static_cast<double>(cur_size) / ids.size();
      if (p > 0.0) out_stats.entropy -= p * std::log2(p);
    }
    out_stats.ave_options = total / (double) ids.size();
    out_stats.solve_p = solve_count / (double) ids.size();
    return out_stats;
  }

  /// Calculate stats just for words in the filter set.
  GroupStats CalcStats(const IDSet & filter_set) {
    GroupStats out_stats;
    IDSet result_group;

    double total = 0.0;
    size_t solve_count = 0; // How many options narrow to a single word?
    for (size_t start_id = 0; start_id < starts.size(); ++start_id) {
      GetGroup(result_group, start_id);
      result_group &= filter_set;
      const size_t cur_size = result_group.size();
      if (cur_size == 1) ++solve_count;
      if (cur_size > out_stats.max_options) out_stats.max_options = cur_size;
      total += cur_size * cur_size;
      double p = static_cast<double>(cur_size) / filter_set.size();
      if (p > 0.0) out_stats.entropy -= p * std::log2(p);
    }
    out_stats.ave_options = total / (double) filter_set.size();
    out_stats.solve_p = solve_count / (double) filter_set.size();
    return out_stats;
  }
};

/// Track the expected stats after multiple clue results are combined..
class MultiGroup {
private:
  emp::vector<size_t> combo_ids;  // Current set of categories.

public:
  MultiGroup() { }

  void Reset() { combo_ids.resize(0); }

  void Add(const IDGroups & groups) {
    // If we dont have the correct number of ids, fix it.
    if (combo_ids.size() == 0) { combo_ids.resize(groups.ids.size()); }

    /// Loop through all the words, updating their associated category.
    uint16_t group_id = 0;
    for (size_t i = 0; i < groups.ids.size(); ++i) {
      // Find the current group id.
      while (group_id+1 < groups.starts.size() && groups.starts[group_id+1] <= i) ++group_id;

      combo_ids[groups.ids[i]] <<= 16;
      combo_ids[groups.ids[i]] += group_id;
    }
  }

  GroupStats CalcStats() {
    GroupStats out_stats;  // Setup the container for the results.
    emp::Sort(combo_ids);  // Sort the group IDs to better track them.
    const double total_count = (double) combo_ids.size();
    combo_ids.push_back((size_t) -1);  // Pad the back of combo ideas for easy stopping.

    double total = 0.0;     // Total choices left for each result answer.
    size_t solve_count = 0; // How many options narrow to a single word?

    size_t pos = 0;
    size_t cur_combo = combo_ids[pos];
    size_t cur_size = 0;
    while (cur_combo != (size_t) -1) {
      size_t next_combo = combo_ids[pos++];
      if (cur_combo == next_combo) { ++cur_size; continue; }

      if (cur_size == 1) ++solve_count;
      if (cur_size > out_stats.max_options) out_stats.max_options = cur_size;
      total += cur_size * cur_size;
      double p = static_cast<double>(cur_size) / total_count;
      if (p > 0.0) out_stats.entropy -= p * std::log2(p);

      // Move on to the next combination.
      cur_combo = next_combo;
      cur_size = 1;
    }
    out_stats.ave_options = total / total_count;
    out_stats.solve_p = solve_count / total_count;

    return out_stats;
  }

};

class WordleEngine {
private:
  static constexpr size_t MAX_LETTER_REPEAT = 4;

  // Get the ID (0-25) associated with a letter.
  static uint16_t ToID(char letter) {
    if (letter >= 'a' && letter <= 'z') return static_cast<uint16_t>(letter - 'a');
    if (letter >= 'A' && letter <= 'Z') return static_cast<uint16_t>(letter - 'A');
    emp_error("Character is not a letter: ", letter);
    return 26;
  }

  static char ToLetter(size_t id) {
    emp_assert(id < 26, id);
    return static_cast<char>(id + 'a');
  }

  Result ToResult(size_t id) const { return Result(word_size, id); }

  // All of the clues for a given position.
  struct PositionClues {
    size_t pos;
    std::array<IDSet, 26> here;      // Is a given letter at this position?
  };

  // All of the clues for zero or more instances of a given letter.
  struct LetterClues {
    size_t letter;  // [0-25]
    std::array<IDSet, MAX_LETTER_REPEAT+1> at_least; ///< Are there at least x instances of letter? (0 is meaningless)
    std::array<IDSet, MAX_LETTER_REPEAT+1> exactly;  ///< Are there exactly x instances of letter?
  };

  struct WordData {
    std::string word;
    GroupStats stats;
    IDGroups next_words;   // What words will each clue lead to?

    WordData() { }
    WordData(const std::string & in_word) : word(in_word) { }
    WordData(const WordData &) = default;
    WordData(WordData &&) = default;

    WordData & operator=(const WordData &) = default;
    WordData & operator=(WordData &&) = default;
  };


  //////////////////////////////////////////////////////////////////////////////////////////
  //  DATA MEMBERS for WorldEngine
  //////////////////////////////////////////////////////////////////////////////////////////

  size_t word_size;
  size_t num_ids;                                  ///< Number of possible result IDs

  emp::vector<WordData> words;                     ///< Data about all words in this Wordle
  emp::vector<PositionClues> pos_clues;            ///< A PositionClues object for each position.
  emp::array<LetterClues,26> let_clues;            ///< Clues based off the number of letters.
  std::unordered_map<std::string, size_t> id_map;  ///< Map of words to their position ids.
  IDSet cur_options;                               ///< Current word options.

  // Pre-process status
  bool clues_ok = true    ;    // Do the clues have associated words?
  bool words_ok = true;        // Do the words have associated result groups?
  bool stats_ok = true;        // Do the words have associated stats?
  size_t words_processed = 0;  // Track how many words are done...

  double progress = 0.0;             // Fraction of progess on loading a file.

public:
  WordleEngine(size_t _word_size)
  : word_size(_word_size)
  , num_ids(Result::CalcNumIDs(_word_size))
  , pos_clues(word_size)
  { }

  WordleEngine(const emp::vector<std::string> & in_words, size_t _word_size)
  : WordleEngine(_word_size) { Load(in_words); }

  WordleEngine(const std::string & filename, size_t _word_size)
  : WordleEngine(_word_size) { Load(filename); }

  size_t GetSize() const { return words.size(); }
  size_t GetNumResults() const { return num_ids; }
  const IDSet & GetOptions() const { return cur_options; }
  IDSet GetAllOptions() const { return IDSet(words.size()); }
  void ResetOptions() {
    cur_options.SetAll(words.size());
    stats_ok = false;
    Process();
  }
  double GetProgress() const { return progress; }


  void SetWordSize(size_t in_size) {
    word_size = in_size;
    num_ids = Result::CalcNumIDs(word_size);
    pos_clues.resize(word_size);
    words.clear();
    cur_options.Clear();
    id_map.clear();
  }

  bool HasWord(const std::string & word) const {
    return emp::Has(id_map, word);
  }

  void AddClue(const std::string & word, Result result) {
    size_t word_id = id_map[word];
    const auto & word_data = words[word_id];
    cur_options &= word_data.next_words.GetGroup(result.GetID());
    stats_ok = false;
    Process();
  }

  IDList SortWords(
    const IDSet & ids,
    const std::string & sort_type="alpha"
  ) const {
    if (sort_type == "max") {
      return ids.GetSorted( [this](uint16_t id1, uint16_t id2) {
        const auto & w1 = words[id1].stats;
        const auto & w2 = words[id2].stats;
        if (w1.max_options == w2.max_options) return w1.ave_options < w2.ave_options; // tiebreak
        return w1.max_options < w2.max_options;
      } );
    } else if (sort_type == "ave") {
      return ids.GetSorted( [this](uint16_t id1, uint16_t id2) {
        const auto & w1 = words[id1].stats;
        const auto & w2 = words[id2].stats;
        if (w1.ave_options == w2.ave_options) return w1.max_options < w2.max_options; // tiebreak
        return w1.ave_options < w2.ave_options;
      } );
    } else if (sort_type == "info") {
      return ids.GetSorted( [this](uint16_t id1, uint16_t id2) {
        const auto & w1 = words[id1].stats;
        const auto & w2 = words[id2].stats;
        return w1.entropy > w2.entropy;
      } );
    } else if (sort_type == "solve") {
      return ids.GetSorted( [this](uint16_t id1, uint16_t id2) {
        const auto & w1 = words[id1].stats;
        const auto & w2 = words[id2].stats;
        return w1.solve_p > w2.solve_p;
      } );
    } else if (sort_type == "alpha") {
      return ids.GetSorted( [this](uint16_t id1, uint16_t id2) {
        const auto & w1 = words[id1];
        const auto & w2 = words[id2];
        return w1.word < w2.word;
      } );
    } else if (sort_type == "r-max") {
      return ids.GetSorted( [this](uint16_t id1, uint16_t id2) {
        const auto & w1 = words[id1].stats;
        const auto & w2 = words[id2].stats;
        if (w1.max_options == w2.max_options) return w1.ave_options > w2.ave_options; // tiebreak
        return w1.max_options > w2.max_options;
      } );
    } else if (sort_type == "r-ave") {
      return ids.GetSorted( [this](uint16_t id1, uint16_t id2) {
        const auto & w1 = words[id1].stats;
        const auto & w2 = words[id2].stats;
        if (w1.ave_options == w2.ave_options) return w1.max_options > w2.max_options; // tiebreak
        return w1.ave_options > w2.ave_options;
      } );
    } else if (sort_type == "r-info") {
      return ids.GetSorted( [this](uint16_t id1, uint16_t id2) {
        const auto & w1 = words[id1].stats;
        const auto & w2 = words[id2].stats;
        return w1.entropy < w2.entropy;
      } );
    } else if (sort_type == "r-alpha") {
      return ids.GetSorted( [this](uint16_t id1, uint16_t id2) {
        const auto & w1 = words[id1];
        const auto & w2 = words[id2];
        return w1.word > w2.word;
      } );
    } else if (sort_type == "r-solve") {
      return ids.GetSorted( [this](uint16_t id1, uint16_t id2) {
        const auto & w1 = words[id1].stats;
        const auto & w2 = words[id2].stats;
        return w1.solve_p < w2.solve_p;
      } );
    }
 
    return IDList();
  }

  IDList SortAllWords(const std::string & sort_type="alpha") const {
    return SortWords(GetAllOptions(), sort_type);
  }

  IDList SortCurWords(const std::string & sort_type="alpha") const {
    return SortWords(GetOptions(), sort_type);
  }

  void WriteWords(
    const IDList & ids,
    std::size_t max_count,
    std::ostream & os=std::cout,
    bool extra_data=true,
    std::string col_break = ", ",
    std::string line_break = "\n"
  ) const {
    for (size_t i = 0; i < max_count && i < ids.size(); ++i) {
      size_t id = ids[i];
      os << words[id].word;
      if (extra_data) {
        os << col_break << words[id].stats.ave_options
          << col_break << words[id].stats.max_options
          << col_break << words[id].stats.entropy
          << col_break << words[id].stats.solve_p;
      }
      os << line_break;
    }
    if (max_count < ids.size()) {
      os << "...plus " << (ids.size() - max_count) << " more." << line_break;
    }
    os.flush();
  }


  /// Load a whole series for words (from a file) into this WordleEngine
  void Load(const emp::vector<std::string> & in_words) {
    // Load in all of the words.
    size_t wrong_size_count = 0;
    size_t invalid_char_count = 0;
    size_t dup_count = 0;
    for (const std::string & in_word : in_words) {
      // Only keep words of the correct size and all lowercase.
      if (in_word.size() != word_size) { wrong_size_count++; continue; }
      if (!emp::is_lower(in_word)) { invalid_char_count++; continue; }
      if (emp::Has(id_map, in_word)) { dup_count++; continue; }
      size_t id = words.size();      // Set a unique ID for this word.
      id_map[in_word] = id;         // Keep track of the ID for this word.
      words.emplace_back(in_word);   // Setup the word data.
    }

    if (wrong_size_count) {
      std::cerr << "Warning: eliminated " << wrong_size_count << " words of the wrong size."
                << std::endl;
    }
    if (invalid_char_count) {
      std::cerr << "Warning: eliminated " << invalid_char_count << " words with invalid characters."
                << std::endl;
    }
    if (dup_count) {
      std::cerr << "Warning: eliminated " << dup_count << " words that were duplicates."
                << std::endl;
    }

    clues_ok = false;
    words_ok = false;
    stats_ok = false;
    ResetOptions();

    std::cout << "Loaded " << words.size() << " valid words." << std::endl;
  }

  /// Load in words from a file.
  void Load(const std::string & filename) {
    std::ifstream is(filename);
    emp::vector<std::string> in_words;
    std::string new_word;
    while (is) {
      is >> new_word;
      in_words.push_back(new_word);
    }
    Load(in_words);
  }

  // Identify which words satisfy each clue.
  bool ProcessClues() {
    std::cout << "Processing Clues!" << std::endl;

    // Setup all of the clue sizes.
    for (size_t i=0; i < word_size; ++i) {
      pos_clues[i].pos = i;
      for (size_t let=0; let < 26; let++) {
        pos_clues[i].here[let].Resize(words.size());
      }
    }
    for (size_t let=0; let < 26; let++) {
      let_clues[let].letter = let;
      for (size_t i = 0; i <= MAX_LETTER_REPEAT; ++i) {
        let_clues[let].at_least[i].Resize(words.size());
        let_clues[let].exactly[i].Resize(words.size());
      }
    }

    // Counters for number of letters.
    emp::array<uint8_t, 26> letter_counts;

    // Loop through each word, indicating which clues it is consistent with.
    for (uint16_t word_id = 0; word_id < words.size(); ++word_id) {
      const std::string & word = words[(size_t) word_id].word;

      // Figure out which letters are in this word.
      std::fill(letter_counts.begin(), letter_counts.end(), 0);      // Reset counters to zero.
      for (const char letter : word) ++letter_counts[ToID(letter)];  // Count letters.

      // Setup the LETTER clues that word is consistent with.
      for (size_t letter_id = 0; letter_id < 26; ++letter_id) { 
        const size_t cur_count = letter_counts[letter_id];       
        let_clues[letter_id].exactly[cur_count].Add(word_id);
        for (uint8_t count = 1; count <= cur_count; ++count) {
          let_clues[letter_id].at_least[count].Add(word_id);
        }
      }

      // Now figure out what POSITION clues it is consistent with.
      for (size_t pos=0; pos < word.size(); ++pos) {
        const size_t cur_letter = ToID(word[pos]);
        pos_clues[pos].here[cur_letter].Add(word_id);
      }
    }

    clues_ok = true;

    return true;
  }
  
  
  /// Given a guess and a result, determine available options.
  // @CAO: OPTIMIZATION NOTES:
  //       - Make words options static to prevent memory allocation.
  //       - Figure out all include/exclude ID sets and run through them from smallest.
  //       - Speed up intersection and subtraction when Set sizes are very different.
  IDSet ProcessResultGroup(const std::string & guess, const Result & result) {
    emp::array<uint8_t, 26> letter_counts;
    std::fill(letter_counts.begin(), letter_counts.end(), 0);
    emp::BitSet<26> letter_fail;
    static IDSet word_options;  // Start with all options available.
    static emp::vector< emp::Ptr<IDSet> > intersect_sets;
    static emp::vector< emp::Ptr<IDSet> > exclude_sets;

    intersect_sets.resize(0);
    exclude_sets.resize(0);

    // First add HERE clues and collect letter counts.
    for (size_t i = 0; i < word_size; ++i) {
      const uint16_t cur_letter = ToID(guess[i]);
      if (result[i] == Result::HERE) {
        intersect_sets.push_back( &pos_clues[i].here[cur_letter] );
        ++letter_counts[cur_letter];
      } else if (result[i] == Result::ELSEWHERE) {
        exclude_sets.push_back( &pos_clues[i].here[cur_letter] );
        ++letter_counts[cur_letter];
      } else {  // Must be 'N'
        exclude_sets.push_back( &pos_clues[i].here[cur_letter] );
        letter_fail.Set(cur_letter);
      }
    }

    // Next add letter frequency clues.
    for (size_t letter_id = 0; letter_id < 26; ++letter_id) { 
      const size_t let_count = letter_counts[letter_id];
      // If a letter failed, we know exactly how many there are.
      if (letter_fail.Has(letter_id)) {
        intersect_sets.push_back( &let_clues[letter_id].exactly[let_count] );
      }

      // Otherwise we know it's at least the number that we tested.
      else if (let_count) {
        intersect_sets.push_back( &let_clues[letter_id].at_least[let_count] );
      }
    }

    // Run through intersections.
    word_options = *intersect_sets[0];
    for (size_t i=1; i < intersect_sets.size(); ++i) word_options &= *intersect_sets[i];
    for (size_t i=0; i < exclude_sets.size(); ++i) word_options -= *exclude_sets[i];

    return word_options;
  }
  
  /// Analyze the current set of words and store result groups for each one.
  void ProcessWords() {
    if (words_processed == 0) {
      progress = 0.0;
      std::cout << "Processing Words!" << std::endl;
    }

    size_t cur_process = 0;  // Track how many words we've processed THIS time through.
    IDSet result_words;

    while (words_processed < words.size()) {
      WordData & word_data = words[words_processed++];
      word_data.next_words.Reset(words.size());

      // Step through each possible result and determine what words that would leave.
      for (size_t result_id = 0; result_id < num_ids; ++result_id) {
        result_words.Clear();
        Result result(word_size, result_id);
        if (result.IsValid(word_data.word)) {
          result_words = ProcessResultGroup(word_data.word, ToResult(result_id));
        }
        word_data.next_words.AddGroup(result_words);
      }

      if (++cur_process >= 100) {
        progress = words_processed / (double) words.size();
        return;
      }
    }

    words_processed = 0; // Reset words processed for next time.
    words_ok = true;     // Do the words have associated result groups?
  }

  /// Loop through all words and update their stats.
  void ProcessStats() {
    std::cout << "Processing Stats!" << std::endl;
    // If we are not limiting the words, this can go fast...
    if (cur_options.size() == words.size()) {
      for (auto & word_data : words) {
        word_data.stats = word_data.next_words.CalcStats();
      }
    } else {
      for (auto & word_data : words) {
        word_data.stats = word_data.next_words.CalcStats(cur_options);
      }
    }
    stats_ok = true;
  }

  /// Handle all of the needed preprocessing.
  bool Process() {
    if (!clues_ok) { ProcessClues(); return false; }
    if (!words_ok) { ProcessWords(); return false; }
    if (!stats_ok) { ProcessStats(); return false; }
    return true;
  }


  void PrintPosClues(size_t pos) const {
    const PositionClues & clue = pos_clues[pos];
    std::cout << "Position " << pos << ":\n";
    for (uint8_t i = 0; i < 26; ++i) {
      std::cout << " '" << ToLetter(i) << "' : ";
      WriteWords(clue.here[i].AsList(), 10, std::cout, false, "", " ");
      std::cout << std::endl;
    }
  }

  void PrintLetterClues(char letter) const {
    const LetterClues & clue = let_clues[ToID(letter)];
    std::cout << "Letter '" << clue.letter << "':\n";
    for (size_t i = 0; i <= MAX_LETTER_REPEAT; ++i) {
      std::cout << "EXACTLY " << i << ":  ";
      WriteWords(clue.exactly[i].AsList(), 20, std::cout, false, "", " ");
      std::cout << std::endl;
    }
    for (size_t i = 0; i <= MAX_LETTER_REPEAT; ++i) {
      std::cout << "AT LEAST " << i << ": ";
      WriteWords(clue.at_least[i].AsList(), 20, std::cout, false, "", " ");
      std::cout << std::endl;
    }
  }

  void AnalyzeStats(const emp::vector<std::string> & in_words) {
    // Collect the word data
    MultiGroup multi;
    for (const auto & word : in_words) {
      uint16_t id = id_map[word];
      multi.Add(words[id].next_words);
    }

    // Calculate the metrics
    GroupStats result = multi.CalcStats();
    
    // Output the results
    std::cout << "Metrics for " << emp::to_quoted_list(in_words) << ":\n"
              << "  expected # of remaining options: " << result.ave_options << "\n"
              << "  maximum # of remaining options:  " << result.max_options << "\n"
              << "  information provided:            " << result.entropy << " bits\n"
              << "  prob. of only one solution left: " << result.solve_p << "\n"
              << std::endl;
  }

  void AnalyzePairs() {
    // Sort words by max individual information.
    auto info_words = SortAllWords("info");

    MultiGroup multi;
    GroupStats best_stats;
    size_t search_count = 0;

    // Prime the best stats with worst-case options.
    best_stats.ave_options = words.size();
    best_stats.max_options = words.size();

    // loop through all pairs of words, starting from front of list.
    for (size_t p1 = 1; p1 < info_words.size(); ++p1) {
      for (size_t p2 = 0; p2 < p1; ++p2) {
        uint16_t w1 = info_words[p1];
        uint16_t w2 = info_words[p2];

        multi.Reset();
        multi.Add(words[w1].next_words);
        multi.Add(words[w2].next_words);

        GroupStats result = multi.CalcStats();

        if (result.ave_options < best_stats.ave_options) { 
          best_stats.ave_options = result.ave_options;
          std::cout << "New best 'AVERAGE' pair: '"
                    << words[w1].word << "' and '" << words[w2].word
                    << "' with a result of "
                    << best_stats.ave_options
                    << std::endl;
        }
        if (result.max_options < best_stats.max_options) { 
          best_stats.max_options = result.max_options;
          std::cout << "New best 'MAXIMUM' pair: '"
                    << words[w1].word << "' and '" << words[w2].word
                    << "' with a result of "
                    << best_stats.max_options
                    << std::endl;
        }
        if (result.entropy > best_stats.entropy) { 
          best_stats.entropy = result.entropy;
          std::cout << "New best 'INFO' pair: '"
                    << words[w1].word << "' and '" << words[w2].word
                    << "' with a result of "
                    << best_stats.entropy
                    << std::endl;
        }
        if (result.solve_p > best_stats.solve_p) { 
          best_stats.solve_p = result.solve_p;
          std::cout << "New best 'SOLVE PROBABILITY' pair: '"
                    << words[w1].word << "' and '" << words[w2].word
                    << "' with a result of "
                    << best_stats.solve_p
                    << std::endl;
        }

        if (++search_count % 10000 == 0) {
          std::cout << "===> Searched " << search_count << " combos.  Just finished '"
                    << words[w1].word << "' and '" << words[w2].word
                    << "'."
                    << std::endl;
        }
      }
    }
  }


  void AnalyzeTriples() {
    // Sort words by max individual information.
    auto info_words = SortAllWords("info");

    MultiGroup multi;
    GroupStats best_stats;
    size_t search_count = 0;

    // Prime the best stats with worst-case options.
    best_stats.ave_options = words.size();
    best_stats.max_options = words.size();

    // loop through all pairs of words, starting from front of list.
    for (size_t p1 = 2; p1 < info_words.size(); ++p1) {
      for (size_t p2 = 1; p2 < p1; ++p2) {
        for (size_t p3 = 0; p3 < p2; ++p3) {
          uint16_t w1 = info_words[p1];
          uint16_t w2 = info_words[p2];
          uint16_t w3 = info_words[p3];

          multi.Reset();
          multi.Add(words[w1].next_words);
          multi.Add(words[w2].next_words);
          multi.Add(words[w3].next_words);

          GroupStats result = multi.CalcStats();

          if (result.ave_options < best_stats.ave_options) { 
            best_stats.ave_options = result.ave_options;
            std::cout << "New best 'AVERAGE' triple: '"
                      << words[w1].word << "', '" << words[w2].word << "' and " << words[w3].word
                      << "' with a result of "
                      << best_stats.ave_options
                      << std::endl;
          }
          if (result.max_options < best_stats.max_options) { 
            best_stats.max_options = result.max_options;
            std::cout << "New best 'MAXIMUM' triple: '"
                      << words[w1].word << "', '" << words[w2].word << "' and " << words[w3].word
                      << "' with a result of "
                      << best_stats.max_options
                      << std::endl;
          }
          if (result.entropy > best_stats.entropy) { 
            best_stats.entropy = result.entropy;
            std::cout << "New best 'INFO' triple: '"
                      << words[w1].word << "', '" << words[w2].word << "' and " << words[w3].word
                      << "' with a result of "
                      << best_stats.entropy
                      << std::endl;
          }
          if (result.solve_p > best_stats.solve_p) { 
            best_stats.solve_p = result.solve_p;
            std::cout << "New best 'SOLVE PROBABILITY' triple: '"
                      << words[w1].word << "', '" << words[w2].word << "' and " << words[w3].word
                      << "' with a result of "
                      << best_stats.solve_p
                      << std::endl;
          }

          if (++search_count % 10000 == 0) {
            std::cout << "===> Searched " << search_count << " combos.  Just finished '"
                      << words[w1].word << "' [" << p1 << "], '"
                      << words[w2].word << "' [" << p2 << "] and '"
                      << words[w3].word << "' [" << p3 << "]."
                      << std::endl;
          }
        }
      }
    }
  }
};
