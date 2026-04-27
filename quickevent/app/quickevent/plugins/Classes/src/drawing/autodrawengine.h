#ifndef DRAWING_AUTODRAWENGINE_H
#define DRAWING_AUTODRAWENGINE_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace drawing {

// One rule in the interval assignment table; evaluated top-to-bottom, first match wins.
struct IntervalRule
{
    QStringList classNames;  // empty = matches any class name
    int maxRunners = -1;     // -1 = no runner-count constraint
    int minInterval = 2;     // minutes

    QVariantMap toVariantMap() const;
    static IntervalRule fromVariantMap(const QVariantMap &m);
};

struct AutoDrawConfig
{
    QList<IntervalRule> intervalRules;
    int gapFactor         = 2;   // gap between consecutive classes in same box = gapFactor * max(I_a, I_b)
    int maxExpandInterval = 10;  // upper limit for rule 6i interval expansion

    static AutoDrawConfig defaults();
    QVariantMap toVariantMap() const;
    static AutoDrawConfig fromVariantMap(const QVariantMap &m);
    static AutoDrawConfig fromSettings();
    void saveToSettings() const;
};

// Per-class data used by the algorithm — loaded from DB, outputs written back.
struct ClassInfo
{
    int     defId        = 0;   // classdefs.id (PK)
    int     classId      = 0;
    int     courseId     = 0;
    int     firstCode    = 0;
    int     entriesCount = 0;   // actual is-running runners (runsCount from SQL) → drives minInterval
    int     totalCount   = 0;   // max(mapCount, runsCount) → drives class duration (includes configured vacants)
    QString className;

    // Outputs written by AutoDrawAlgorithm::compute():
    int assignedInterval     = 0;
    int startSlotIndex       = -1;
    int startTimeMin         = 0;
    int computedVacantsAfter = 0;  // extra trailing vacants added by 6i overflow
};

// Pure algorithm — no DB, no Qt plugins. Instantiable from tests directly.
class AutoDrawAlgorithm
{
public:
    struct CourseGroup
    {
        int courseId  = 0;
        int firstCode = 0;
        QList<ClassInfo *> classes;  // ordering = SQL result order (user intent)
        int startSlotIndex = -1;
        int groupOffset    = 0;      // minute offset for first-control stagger
    };

    explicit AutoDrawAlgorithm(const AutoDrawConfig &cfg);

    // Fills assignedInterval, startSlotIndex, startTimeMin, computedVacantsAfter
    // on every element of 'classes'. Returns the resulting group arrangement.
    QList<CourseGroup> compute(QList<ClassInfo> &classes);

    int matchInterval(const ClassInfo &ci) const;  // public for tests

private:
    int  groupLastRunnerTime(const CourseGroup &g) const;
    void expandGroupIntervals(CourseGroup &g, int targetDuration);
    void resolveFirstCodeConflicts(QList<CourseGroup> &groups);
    void assignStartTimes(QList<CourseGroup> &groups);

    AutoDrawConfig m_cfg;
};

// DB wrapper: loads class data, calls AutoDrawAlgorithm, saves results.
// Plain (non-QObject) class — no signals/slots needed.
class AutoDrawEngine
{
public:
    explicit AutoDrawEngine(int stageId, const AutoDrawConfig &cfg);

    // Returns false when no classes with entries are found for the stage.
    bool run();

private:
    QList<ClassInfo> loadClassData() const;
    void saveResult(const QList<AutoDrawAlgorithm::CourseGroup> &groups,
                    const QList<ClassInfo> &classes);

    int            m_stageId;
    AutoDrawConfig m_cfg;
};

}  // namespace drawing

#endif // DRAWING_AUTODRAWENGINE_H
