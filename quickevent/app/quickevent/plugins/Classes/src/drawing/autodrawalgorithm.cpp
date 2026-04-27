#include "autodrawengine.h"

#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <numeric>

namespace drawing {

// ───────────────────────────── IntervalRule ──────────────────────────────────

QVariantMap IntervalRule::toVariantMap() const
{
    QVariantMap m;
    if (!classNames.isEmpty())
        m["classNames"] = classNames.join(",");
    if (maxRunners >= 0)
        m["maxRunners"] = maxRunners;
    m["minInterval"] = minInterval;
    return m;
}

IntervalRule IntervalRule::fromVariantMap(const QVariantMap &m)
{
    IntervalRule r;
    if (m.contains("classNames"))
        r.classNames = m["classNames"].toString().split(",", Qt::SkipEmptyParts);
    r.maxRunners  = m.value("maxRunners", -1).toInt();
    r.minInterval = m.value("minInterval", 2).toInt();
    return r;
}

// ───────────────────────────── AutoDrawConfig ─────────────────────────────────

AutoDrawConfig AutoDrawConfig::defaults()
{
    AutoDrawConfig c;
    c.gapFactor         = 2;
    c.maxExpandInterval = 10;
    c.intervalRules = {
        { {"H12C","H14C","D12C","D14C"}, -1, 3 },
        { {},  8, 6 },
        { {}, 15, 3 },
        { {}, -1, 2 },
    };
    return c;
}

QVariantMap AutoDrawConfig::toVariantMap() const
{
    QVariantMap m;
    m["gapFactor"]         = gapFactor;
    m["maxExpandInterval"] = maxExpandInterval;
    QVariantList rules;
    for (const IntervalRule &r : intervalRules)
        rules << r.toVariantMap();
    m["intervalRules"] = rules;
    return m;
}

AutoDrawConfig AutoDrawConfig::fromVariantMap(const QVariantMap &m)
{
    AutoDrawConfig c;
    c.gapFactor         = m.value("gapFactor",         2).toInt();
    c.maxExpandInterval = m.value("maxExpandInterval", 10).toInt();
    for (const QVariant &v : m.value("intervalRules").toList())
        c.intervalRules << IntervalRule::fromVariantMap(v.toMap());
    if (c.intervalRules.isEmpty())
        c.intervalRules = defaults().intervalRules;
    return c;
}

AutoDrawConfig AutoDrawConfig::fromSettings()
{
    QSettings s;
    s.beginGroup("autodraw");
    QVariant v = s.value("config");
    s.endGroup();
    return v.isValid() ? fromVariantMap(v.toMap()) : defaults();
}

void AutoDrawConfig::saveToSettings() const
{
    QSettings s;
    s.beginGroup("autodraw");
    s.setValue("config", toVariantMap());
    s.endGroup();
}

// ───────────────────────────── AutoDrawAlgorithm ─────────────────────────────

AutoDrawAlgorithm::AutoDrawAlgorithm(const AutoDrawConfig &cfg)
    : m_cfg(cfg)
{}

int AutoDrawAlgorithm::matchInterval(const ClassInfo &ci) const
{
    for (const IntervalRule &rule : m_cfg.intervalRules) {
        if (!rule.classNames.isEmpty()
                && !rule.classNames.contains(ci.className, Qt::CaseInsensitive))
            continue;
        if (rule.maxRunners >= 0 && ci.entriesCount > rule.maxRunners)
            continue;
        return rule.minInterval;
    }
    return 2;
}

int AutoDrawAlgorithm::groupLastRunnerTime(const CourseGroup &g) const
{
    int time  = 0;
    int prevI = 0;
    for (const ClassInfo *ci : g.classes) {
        if (prevI > 0)
            time += m_cfg.gapFactor * qMax(prevI, ci->assignedInterval);
        time += (ci->totalCount - 1) * ci->assignedInterval;
        prevI = ci->assignedInterval;
    }
    return time;
}

void AutoDrawAlgorithm::expandGroupIntervals(CourseGroup &g, int targetDuration)
{
    while (groupLastRunnerTime(g) < targetDuration) {
        ClassInfo *best = nullptr;
        for (ClassInfo *ci : g.classes) {
            if (ci->assignedInterval < m_cfg.maxExpandInterval) {
                if (!best || ci->totalCount > best->totalCount)
                    best = ci;
            }
        }
        if (!best) {
            int remaining = targetDuration - groupLastRunnerTime(g);
            if (remaining > 0 && m_cfg.maxExpandInterval > 0)
                g.classes.last()->computedVacantsAfter += remaining / m_cfg.maxExpandInterval;
            break;
        }
        best->assignedInterval++;
    }
}

static int smallestCompatibleInterval(int minVal, int fixedI, int cap)
{
    for (int I = minVal; I <= cap; I++) {
        if (std::gcd(I, fixedI) > 1)
            return I;
    }
    return qMin(fixedI, cap);
}

void AutoDrawAlgorithm::resolveFirstCodeConflicts(QList<CourseGroup> &groups)
{
    QMap<int, QList<int>> byFirstCode;
    for (int idx = 0; idx < groups.size(); idx++)
        byFirstCode[groups[idx].firstCode].append(idx);

    for (const QList<int> &bucket : byFirstCode) {
        if (bucket.size() < 2)
            continue;

        for (int pass = 0; pass < 5; pass++) {
            bool anyFixed = false;
            for (int bi = 0; bi < bucket.size(); bi++) {
                for (int bj = bi + 1; bj < bucket.size(); bj++) {
                    CourseGroup &Gi = groups[bucket[bi]];
                    CourseGroup &Gj = groups[bucket[bj]];
                    for (ClassInfo *ci : Gi.classes) {
                        for (ClassInfo *cj : Gj.classes) {
                            if (std::gcd(ci->assignedInterval, cj->assignedInterval) > 1)
                                continue;
                            int totalI = 0, totalJ = 0;
                            for (const ClassInfo *x : Gi.classes) totalI += x->totalCount;
                            for (const ClassInfo *x : Gj.classes) totalJ += x->totalCount;
                            ClassInfo *victim = (totalI <= totalJ) ? ci : cj;
                            int fixedI = (victim == ci) ? cj->assignedInterval
                                                        : ci->assignedInterval;
                            int newI = smallestCompatibleInterval(
                                victim->assignedInterval, fixedI, m_cfg.maxExpandInterval);
                            if (newI != victim->assignedInterval) {
                                victim->assignedInterval = newI;
                                anyFixed = true;
                            }
                        }
                    }
                }
            }
            if (!anyFixed)
                break;
        }

        for (int i = 0; i < bucket.size(); i++)
            groups[bucket[i]].groupOffset = i;
    }
}

void AutoDrawAlgorithm::assignStartTimes(QList<CourseGroup> &groups)
{
    for (CourseGroup &g : groups) {
        int time  = g.groupOffset;
        int prevI = 0;
        for (ClassInfo *ci : g.classes) {
            if (prevI > 0)
                time += m_cfg.gapFactor * qMax(prevI, ci->assignedInterval);
            ci->startTimeMin   = time;
            ci->startSlotIndex = g.startSlotIndex;
            time += (ci->totalCount - 1) * ci->assignedInterval;
            prevI = ci->assignedInterval;
        }
    }
}

QList<AutoDrawAlgorithm::CourseGroup> AutoDrawAlgorithm::compute(QList<ClassInfo> &classes)
{
    for (ClassInfo &ci : classes)
        ci.assignedInterval = matchInterval(ci);

    QList<CourseGroup> groups;
    QMap<int, int> courseIdToGroupIndex;
    for (ClassInfo &ci : classes) {
        if (!courseIdToGroupIndex.contains(ci.courseId)) {
            CourseGroup g;
            g.courseId  = ci.courseId;
            g.firstCode = ci.firstCode;
            courseIdToGroupIndex[ci.courseId] = groups.size();
            groups.append(g);
        }
        groups[courseIdToGroupIndex[ci.courseId]].classes.append(&ci);
    }

    int maxDuration = 0;
    for (const CourseGroup &g : groups)
        maxDuration = qMax(maxDuration, groupLastRunnerTime(g));

    for (CourseGroup &g : groups) {
        if (groupLastRunnerTime(g) < maxDuration)
            expandGroupIntervals(g, maxDuration);
    }

    resolveFirstCodeConflicts(groups);

    maxDuration = 0;
    for (const CourseGroup &g : groups)
        maxDuration = qMax(maxDuration, groupLastRunnerTime(g));

    QMap<int, int> firstCodeMaxDur;
    for (const CourseGroup &g : groups) {
        int d = groupLastRunnerTime(g);
        firstCodeMaxDur[g.firstCode] = qMax(firstCodeMaxDur.value(g.firstCode, 0), d);
    }
    std::stable_sort(groups.begin(), groups.end(),
                     [&](const CourseGroup &a, const CourseGroup &b) {
        if (a.firstCode != b.firstCode)
            return firstCodeMaxDur[a.firstCode] > firstCodeMaxDur[b.firstCode];
        return groupLastRunnerTime(a) > groupLastRunnerTime(b);
    });

    for (int i = 0; i < groups.size(); i++)
        groups[i].startSlotIndex = i;

    assignStartTimes(groups);

    return groups;
}

}  // namespace drawing
