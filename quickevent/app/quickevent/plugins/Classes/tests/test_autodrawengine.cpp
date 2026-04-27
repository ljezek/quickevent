#include "autodrawengine.h"

#include <QtTest/QtTest>

using namespace drawing;

// Helper: build a ClassInfo for testing
static ClassInfo makeClass(int defId, int courseId, int firstCode,
                           int entriesCount, const QString &className = {},
                           int mapCount = 0)
{
    ClassInfo ci;
    ci.defId        = defId;
    ci.classId      = defId;
    ci.courseId     = courseId;
    ci.firstCode    = firstCode;
    ci.entriesCount = entriesCount;
    ci.totalCount   = mapCount > 0 ? mapCount : entriesCount;
    ci.className    = className;
    return ci;
}

class TestAutoDrawEngine : public QObject
{
    Q_OBJECT

private slots:
    // ── Test 1 ────────────────────────────────────────────────────────────────
    // Basic grouping and interval assignment.
    // 3 classes on 2 courses (2 share same course), different runner counts.
    void test_basicGroupingAndInterval()
    {
        QList<ClassInfo> classes = {
            makeClass(1, 10, 101, 30),   // 30 runners → interval 2
            makeClass(2, 10, 101,  5),   // same course, 5 runners → interval 6
            makeClass(3, 20, 201, 12),   // different course, 12 runners → interval 3
        };

        AutoDrawAlgorithm algo(AutoDrawConfig::defaults());
        auto groups = algo.compute(classes);

        // Must produce exactly 2 groups
        QCOMPARE(groups.size(), 2);

        // Both classes on course 10 must be in the same group
        int grpCourse10 = -1, grpCourse20 = -1;
        for (int i = 0; i < groups.size(); i++) {
            if (groups[i].courseId == 10) grpCourse10 = i;
            if (groups[i].courseId == 20) grpCourse20 = i;
        }
        QVERIFY(grpCourse10 >= 0);
        QVERIFY(grpCourse20 >= 0);
        QCOMPARE(groups[grpCourse10].classes.size(), 2);
        QCOMPARE(groups[grpCourse20].classes.size(), 1);

        // Interval assignment per default rules
        QCOMPARE(algo.matchInterval(classes[0]), 2);  // 30 runners
        QCOMPARE(algo.matchInterval(classes[1]), 6);  // 5 runners (≤ 8)
        QCOMPARE(algo.matchInterval(classes[2]), 3);  // 12 runners (≤ 15)
    }

    // ── Test 2 ────────────────────────────────────────────────────────────────
    // Critical path detection and rule 6i expansion.
    // Group A is the critical (longest) group; group B must be expanded to match.
    void test_criticalPathAndExpansion()
    {
        // Group A: 50 runners × interval 2 → last runner at (50-1)*2 = 98 min
        // Group B:  8 runners × interval 6 → last runner at  (8-1)*6 = 42 min
        QList<ClassInfo> classes = {
            makeClass(1, 10, 101, 50),
            makeClass(2, 20, 201,  8),
        };

        AutoDrawConfig cfg = AutoDrawConfig::defaults();
        AutoDrawAlgorithm algo(cfg);
        auto groups = algo.compute(classes);

        QCOMPARE(groups.size(), 2);

        // Find group A and B by courseId
        const AutoDrawAlgorithm::CourseGroup *gA = nullptr, *gB = nullptr;
        for (const auto &g : groups) {
            if (g.courseId == 10) gA = &g;
            if (g.courseId == 20) gB = &g;
        }
        QVERIFY(gA != nullptr);
        QVERIFY(gB != nullptr);

        // Group A last runner time must still be 98
        // (entriesCount == totalCount for these test fixtures)
        QCOMPARE(gA->classes[0]->assignedInterval, 2);

        // Group B can't reach 98 with 8 runners (max is (8-1)*10=70),
        // so interval must be maxed out and the slack goes to computedVacantsAfter.
        QCOMPARE(gB->classes[0]->assignedInterval, cfg.maxExpandInterval);
        QVERIFY(gB->classes[0]->computedVacantsAfter > 0);
    }

    // ── Test 3 ────────────────────────────────────────────────────────────────
    // First-control conflict when gcd > 1 (compatible intervals).
    // No interval change expected; only a 1-minute offset on the second group.
    void test_firstCodeConflict_gcdCompatible()
    {
        // Group A: course 10, firstCode 101, interval → 2 (30 runners)
        // Group B: course 20, firstCode 101, interval → 4 (8 runners, but ≤8 → would be 6)
        // Use mapCount trick to control totalCount without changing interval selection.
        // Easier: just use 30 runners for A (interval 2) and specify interval directly
        // by giving 8 runners (interval 6 per defaults) for B.
        // We need intervals with gcd > 1. Let's use 20 runners (interval 2) and 8 runners (interval 6): gcd(2,6)=2 ✓

        QList<ClassInfo> classes = {
            makeClass(1, 10, 101, 20),  // interval 2
            makeClass(2, 20, 101,  8),  // interval 6  (gcd(2,6)=2 > 1 → no change needed)
        };

        AutoDrawConfig cfg = AutoDrawConfig::defaults();
        AutoDrawAlgorithm algo(cfg);
        auto groups = algo.compute(classes);

        QCOMPARE(groups.size(), 2);

        const AutoDrawAlgorithm::CourseGroup *gA = nullptr, *gB = nullptr;
        for (const auto &g : groups) {
            if (g.courseId == 10) gA = &g;
            if (g.courseId == 20) gB = &g;
        }
        QVERIFY(gA && gB);

        int iA = gA->classes[0]->assignedInterval;
        int iB = gB->classes[0]->assignedInterval;

        // gcd must be > 1
        QVERIFY(std::gcd(iA, iB) > 1);

        // Groups must be staggered by exactly 1 minute
        int offA = gA->groupOffset;
        int offB = gB->groupOffset;
        QVERIFY(offA == 0 || offB == 0);        // one of them starts at 0
        QCOMPARE(qAbs(offA - offB), 1);          // staggered by 1 minute

        // Clash condition: (offA - offB) % gcd must != 0
        QVERIFY(qAbs(offA - offB) % std::gcd(iA, iB) != 0);
    }

    // ── Test 4 ────────────────────────────────────────────────────────────────
    // First-control conflict when gcd == 1 (coprime intervals — 2 and 3).
    // Algorithm must raise one interval to make gcd > 1.
    void test_firstCodeConflict_gcdOne()
    {
        // 30 runners → interval 2; 12 runners → interval 3; gcd(2,3)=1 → must fix
        QList<ClassInfo> classes = {
            makeClass(1, 10, 101, 30),  // interval 2
            makeClass(2, 20, 101, 12),  // interval 3
        };

        AutoDrawConfig cfg = AutoDrawConfig::defaults();
        AutoDrawAlgorithm algo(cfg);
        auto groups = algo.compute(classes);

        QCOMPARE(groups.size(), 2);

        int iA = -1, iB = -1;
        for (const auto &g : groups) {
            if (g.courseId == 10) iA = g.classes[0]->assignedInterval;
            if (g.courseId == 20) iB = g.classes[0]->assignedInterval;
        }
        QVERIFY(iA > 0 && iB > 0);

        // After resolution, gcd must be > 1
        QVERIFY(std::gcd(iA, iB) > 1);

        // The fixed interval must not exceed maxExpandInterval
        QVERIFY(iA <= cfg.maxExpandInterval);
        QVERIFY(iB <= cfg.maxExpandInterval);
    }

    // ── Test 5 ────────────────────────────────────────────────────────────────
    // Special class names (H12C, D14C) get 3-minute minimum regardless of runner count.
    void test_specialClassNameInterval()
    {
        AutoDrawConfig cfg = AutoDrawConfig::defaults();
        AutoDrawAlgorithm algo(cfg);

        ClassInfo c1 = makeClass(1, 10, 101, 30, "H12C");
        ClassInfo c2 = makeClass(2, 10, 101,  3, "D14C");
        ClassInfo c3 = makeClass(3, 10, 101, 30, "H21E");

        QCOMPARE(algo.matchInterval(c1), 3);   // special class name wins over runner count
        QCOMPARE(algo.matchInterval(c2), 3);   // special class, 3 runners (would be 6 otherwise)
        QCOMPARE(algo.matchInterval(c3), 2);   // normal large class
    }
};

QTEST_MAIN(TestAutoDrawEngine)
#include "test_autodrawengine.moc"
