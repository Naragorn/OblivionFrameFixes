// Checks the variable lookup cache (VarLookupLogic.h): the engine's layout,
// the walk xOBSE inlines, when a list is walked and when indexed, that an
// index answers exactly what the walk would - first entry of an id, entries
// without a variable, ids out of range - and that it is dropped when the
// list's head changes, when the engine frees the list's variables (also when
// a new list then takes the freed one's memory), to make room, and when the
// budget does not allow it.

#include "VarLookupLogic.h"

#include <cstddef>
#include <cstdio>
#include <vector>

using namespace framefixes;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
	std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", what);
	if (!condition) {
		++g_failures;
	}
}

// A list as the engine builds it: entries in a chain, each with its variable.
// An id of 0xFFFFFFFF stands for an entry without a variable.
struct TestList {
	EngineEventList list{};
	std::vector<EngineVar> vars;
	std::vector<EngineVarEntry> entries;

	explicit TestList(const std::vector<UInt32>& ids) { Fill(ids); }

	void Fill(const std::vector<UInt32>& ids) {
		vars.assign(ids.size(), EngineVar{});
		entries.assign(ids.size(), EngineVarEntry{});
		for (std::size_t i = 0; i < ids.size(); ++i) {
			vars[i].id = ids[i];
			vars[i].data = static_cast<double>(i);
			entries[i].var = ids[i] == 0xFFFFFFFFu ? nullptr : &vars[i];
			entries[i].next = i + 1 < ids.size() ? &entries[i + 1] : nullptr;
		}
		list.vars = ids.empty() ? nullptr : &entries[0];
	}
};

std::vector<UInt32> Sequence(UInt32 first, UInt32 count) {
	std::vector<UInt32> ids;
	for (UInt32 i = 0; i < count; ++i) ids.push_back(first + i);
	return ids;
}

// Every id from 0 to past the largest answered as the walk answers it.
bool AgreesWithWalk(VarLookupCache& cache, const EngineEventList& list, UInt32 upTo) {
	for (UInt32 id = 0; id <= upTo; ++id) {
		if (cache.Find(&list, id) != WalkVariables(list, id, nullptr)) return false;
	}
	return true;
}

void TestLayout() {
	std::printf("The engine's layout (xOBSE GameAPI.h)\n");
	Check(offsetof(EngineEventList, vars) == 0x0C && sizeof(EngineEventList) == 0x14,
	      "the variables at +0x0C of a 0x14-byte list (0x004FBDC0 allocates 0x14)");
	Check(offsetof(EngineVar, id) == 0 && offsetof(EngineVar, data) == 8 && sizeof(EngineVar) == 0x10,
	      "a variable: id at +0, value at +8");
	Check(offsetof(EngineVarEntry, var) == 0 && offsetof(EngineVarEntry, next) == 4 && sizeof(EngineVarEntry) == 8,
	      "an entry: variable at +0, next at +4");
}

void TestWalk() {
	std::printf("The walk xOBSE inlines\n");
	TestList t({3, 0xFFFFFFFFu, 5, 7, 5, 0x10009});
	UInt32 nodes = 0;
	Check(WalkVariables(t.list, 5, &nodes) == &t.vars[2] && nodes == 3, "the first entry of an id, three looked at");
	Check(WalkVariables(t.list, 7, nullptr) == &t.vars[3], "past an entry without a variable");
	Check(WalkVariables(t.list, 8, &nodes) == nullptr && nodes == 6, "a missing id: null, after every entry");
	Check(WalkVariables(t.list, 9, nullptr) == nullptr, "a 32-bit id past 0xFFFF never meets the 16-bit index");
	Check(WalkVariables(t.list, 0x10005, nullptr) == &t.vars[2], "the asked id is cut to its 16 bits, as ReadFrom reads it");
	TestList empty({});
	Check(WalkVariables(empty.list, 1, &nodes) == nullptr && nodes == 0, "an empty list: null, nothing looked at");
}

void TestShortList() {
	std::printf("A short list is walked\n");
	VarLookupCache cache;
	TestList t(Sequence(1, 10));
	bool same = true;
	for (int i = 0; i < 100; ++i) same = same && cache.Find(&t.list, 1 + i % 12) == WalkVariables(t.list, 1 + i % 12, nullptr);
	Check(same, "every answer the walk's");
	Check(!cache.Indexed(&t.list) && cache.Stats().built == 0 && cache.Stats().walked == 100,
	      "ten variables: never indexed");

	// A long list asked only for ids near its head is walked as well: such a
	// walk is as short as a short list's.
	TestList longOne(Sequence(1, 3532));
	for (int i = 0; i < 100; ++i) cache.Find(&longOne.list, 1 + i % (VarLookupCache::kLongList - 1));
	Check(!cache.Indexed(&longOne.list) && cache.Stats().built == 0, "a long list asked near its head: never indexed");
}

void TestLongList() {
	std::printf("A long list gets an index\n");
	VarLookupCache cache;
	TestList t(Sequence(1, 3532));   // MOOScript's 3532 variables
	for (UInt32 i = 0; i + 1 < VarLookupCache::kLookupsBeforeIndex; ++i) cache.Find(&t.list, 3000);
	Check(!cache.Indexed(&t.list), "walked until the fourth lookup");
	Check(cache.Find(&t.list, 3000) == &t.vars[2999], "the fourth lookup still walks, and builds");
	Check(cache.Indexed(&t.list) && cache.Stats().built == 1, "indexed");
	const UInt32 walkedBefore = cache.Stats().walked;
	Check(cache.Find(&t.list, 3532) == &t.vars[3531] && cache.Stats().walked == walkedBefore,
	      "then answered by the index");
	Check(AgreesWithWalk(cache, t.list, 3600), "ids 0 to 3600 answered as the walk answers them");
	Check(cache.Find(&t.list, 0xFFFF) == nullptr && cache.Find(&t.list, 0x10BCC) == &t.vars[3019],
	      "past the index: null; the asked id cut to 16 bits");
	Check(cache.PointersHeld() == 3533, "the index holds one pointer per id up to the largest");

	// The first entry of an id wins, entries without a variable are skipped,
	// ids past 0xFFFF are left out - as in the walk.
	std::vector<UInt32> ids = Sequence(1, 40);
	ids[10] = 5;               // a second 5, after the first
	ids[11] = 0xFFFFFFFFu;     // an entry without a variable
	ids[12] = 0x10020;         // a 32-bit id the 16-bit index never meets
	TestList odd(ids);
	VarLookupCache second;
	for (UInt32 i = 0; i < VarLookupCache::kLookupsBeforeIndex; ++i) second.Find(&odd.list, 40);
	Check(second.Indexed(&odd.list), "the odd list indexed");
	Check(second.Find(&odd.list, 5) == &odd.vars[4], "a doubled id: the first entry");
	Check(second.Find(&odd.list, 0x20) == &odd.vars[31], "the 16-bit 0x20 is the variable with id 0x20, not 0x10020");
	Check(AgreesWithWalk(second, odd.list, 64), "every id as the walk answers it");
}

void TestHeadChange() {
	std::printf("A list whose head changed is indexed anew\n");
	VarLookupCache cache;
	TestList t(Sequence(1, 100));
	for (UInt32 i = 0; i < VarLookupCache::kLookupsBeforeIndex; ++i) cache.Find(&t.list, 50);
	Check(cache.Indexed(&t.list), "indexed");
	TestList other(Sequence(200, 100));
	t.list.vars = other.list.vars;   // built anew somewhere without 0x004FA080
	Check(cache.Find(&t.list, 250) == &other.vars[50] && cache.Find(&t.list, 50) == nullptr,
	      "the new variables are answered, the old ones are gone");
	Check(cache.Stats().rebuilt == 1 && !cache.Indexed(&t.list) && cache.IndexesHeld() == 0, "the old index dropped");
	for (UInt32 i = 0; i < VarLookupCache::kLookupsBeforeIndex; ++i) cache.Find(&t.list, 250);
	Check(cache.Indexed(&t.list) && AgreesWithWalk(cache, t.list, 320), "and indexed again, right");
}

void TestForget() {
	std::printf("The engine frees a list's variables\n");
	VarLookupCache cache;
	TestList t(Sequence(1, 100));
	for (UInt32 i = 0; i < VarLookupCache::kLookupsBeforeIndex; ++i) cache.Find(&t.list, 50);
	Check(cache.Indexed(&t.list), "indexed");
	cache.Forget(&t.list);
	Check(!cache.Indexed(&t.list) && cache.Stats().forgotten == 1 && cache.PointersHeld() == 0,
	      "forgotten, its memory given back");
	// A new list at the freed one's address, its entries at the freed entries'
	// addresses - the head the same pointer - holding other variables: what
	// made xOBSE switch the engine's own cache off.
	TestList reused(Sequence(500, 100));
	for (std::size_t i = 0; i < t.entries.size(); ++i) t.entries[i].var = &reused.vars[i];
	Check(cache.Find(&t.list, 550) == &reused.vars[50] && cache.Find(&t.list, 50) == nullptr,
	      "the same addresses with new variables: the new ones answered");
	cache.Forget(&t.list);
	TestList never(Sequence(1, 5));
	cache.Forget(&never.list);
	Check(cache.Stats().forgotten == 1, "forgetting a list without an index changes nothing");
}

void TestEviction() {
	std::printf("Lists that share a set\n");
	VarLookupCache cache;
	std::vector<TestList*> lists;
	std::vector<TestList> pool;
	pool.reserve(4096);
	for (int i = 0; i < 4096 && lists.size() < 3; ++i) {
		pool.emplace_back(Sequence(1, 64));
		if (VarLookupCache::SetFor(&pool.back().list) == VarLookupCache::SetFor(&pool.front().list)) {
			lists.push_back(&pool.back());
		}
	}
	Check(lists.size() == 3, "three lists of one set found");
	if (lists.size() != 3) return;
	TestList& a = *lists[0];
	TestList& b = *lists[1];
	TestList& c = *lists[2];
	for (UInt32 i = 0; i < VarLookupCache::kLookupsBeforeIndex; ++i) cache.Find(&a.list, 60);
	for (UInt32 i = 0; i < VarLookupCache::kLookupsBeforeIndex; ++i) cache.Find(&b.list, 60);
	Check(cache.Indexed(&a.list) && cache.Indexed(&b.list) && cache.IndexesHeld() == 2, "two lists of a set both indexed");
	cache.Find(&a.list, 61);   // a used more recently than b
	for (UInt32 i = 0; i < VarLookupCache::kLookupsBeforeIndex; ++i) cache.Find(&c.list, 60);
	Check(cache.Indexed(&a.list) && !cache.Indexed(&b.list) && cache.Indexed(&c.list) && cache.Stats().evicted == 1,
	      "a third takes the way used longer ago");
	Check(AgreesWithWalk(cache, a.list, 70) && AgreesWithWalk(cache, b.list, 70) && AgreesWithWalk(cache, c.list, 70),
	      "all three answered right");
}

void TestBudget() {
	std::printf("The budget\n");
	VarLookupCache cache(100);
	TestList t(Sequence(1, 3532));
	for (UInt32 i = 0; i < 2 * VarLookupCache::kLookupsBeforeIndex; ++i) cache.Find(&t.list, 3000);
	Check(!cache.Indexed(&t.list) && cache.Stats().refused == 2 && cache.PointersHeld() == 0,
	      "an index past the budget is refused, asked again after as many lookups");
	Check(AgreesWithWalk(cache, t.list, 3600), "and the walk answers");

	VarLookupCache empty;
	std::vector<UInt32> none(20, 0xFFFFFFFFu);
	TestList hollow(none);
	for (UInt32 i = 0; i < VarLookupCache::kLookupsBeforeIndex; ++i) empty.Find(&hollow.list, 1);
	Check(!empty.Indexed(&hollow.list) && empty.Stats().refused == 1 && empty.Find(&hollow.list, 1) == nullptr,
	      "a long list with no variable to index: refused, still null");

	VarLookupCache cleared;
	for (UInt32 i = 0; i < VarLookupCache::kLookupsBeforeIndex; ++i) cleared.Find(&t.list, 3000);
	Check(cleared.PointersHeld() == 3533, "an index held");
	cleared.Clear();
	Check(cleared.PointersHeld() == 0 && !cleared.Indexed(&t.list), "clear gives every index back");
}

}  // namespace

int main() {
	TestLayout();
	TestWalk();
	TestShortList();
	TestLongList();
	TestHeadChange();
	TestForget();
	TestEviction();
	TestBudget();
	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
