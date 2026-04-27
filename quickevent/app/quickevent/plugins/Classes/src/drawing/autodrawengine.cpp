#include "autodrawengine.h"
#include "ganttitem.h"
#include "startslotitem.h"

#include <plugins/Event/src/eventplugin.h>
#include <plugins/Event/src/stage.h>

#include <qf/gui/framework/mainwindow.h>
#include <qf/core/sql/query.h>
#include <qf/core/sql/connection.h>
#include <qf/core/sql/querybuilder.h>
#include <qf/core/utils.h>
#include <qf/core/exception.h>

namespace qfs = qf::core::sql;
using qf::gui::framework::getPlugin;
using Event::EventPlugin;

namespace drawing {

AutoDrawEngine::AutoDrawEngine(int stageId, const AutoDrawConfig &cfg)
    : m_stageId(stageId), m_cfg(cfg)
{}

QList<ClassInfo> AutoDrawEngine::loadClassData() const
{
    qfs::Query q(qfs::Connection::forName());

    qf::core::sql::QueryBuilder qb1;
    qb1.select("competitors.classId")
       .select("COUNT(runs.competitorId) AS runsCount")
       .from("competitors")
       .joinRestricted("competitors.id", "runs.competitorId",
                       "runs.isRunning AND runs.stageId=" QF_IARG(m_stageId))
       .groupBy("competitors.classId")
       .as("classruns");

    qf::core::sql::QueryBuilder qb;
    qb.select2("classdefs", "id, classId, courseId, mapCount")
      .select2("classes",   "name AS className")
      .select2("codes",     "code AS firstCode")
      .select2("classruns", "runsCount")
      .from("classdefs")
      .innerJoin("classdefs.classId", "classes.id")
      .join("classdefs.courseId", "courses.id")
      .innerJoinRestricted("classdefs.courseId", "coursecodes.courseId",
                           "coursecodes.position=1")
      .innerJoin("coursecodes.codeId", "codes.id")
      .joinQuery("classdefs.classId", qb1, "classId",
                 qf::core::sql::QueryBuilder::INNER_JOIN)
      .where("classdefs.stageId=" QF_IARG(m_stageId))
      .orderBy("classdefs.courseId, classes.name");

    q.exec(qb.toString());

    QList<ClassInfo> result;
    while (q.next()) {
        int runsCount = q.value("runsCount").toInt();
        if (runsCount <= 0)
            continue;
        ClassInfo ci;
        ci.defId        = q.value("id").toInt();
        ci.classId      = q.value("classId").toInt();
        ci.courseId     = q.value("courseId").toInt();
        ci.firstCode    = q.value("firstCode").toInt();
        ci.className    = q.value("className").toString();
        ci.entriesCount = runsCount;
        int mapCount    = q.value("mapCount").toInt();
        ci.totalCount   = qMax(mapCount, runsCount);
        result.append(ci);
    }
    return result;
}

void AutoDrawEngine::saveResult(const QList<AutoDrawAlgorithm::CourseGroup> &groups,
                                const QList<ClassInfo> &classes)
{
    {
        Event::StageData stage = getPlugin<EventPlugin>()->stageData(m_stageId);
        DrawingConfig dc(stage.drawingConfig());

        QVariantList startSlots;
        for (const AutoDrawAlgorithm::CourseGroup &g : groups) {
            StartSlotData sd;
            sd.setStartOffset(g.classes.isEmpty() ? 0 : g.classes.first()->startTimeMin);
            sd.setIgnoreClassClashCheck(false);
            startSlots << sd;
        }
        dc.setStartSlots(startSlots);

        QString dcStr = qf::core::Utils::qvariantToJson(dc);
        qfs::Query q(qfs::Connection::forName());
        q.prepare("UPDATE stages SET drawingConfig=:drawingConfig WHERE id=:id",
                  qf::core::Exception::Throw);
        q.bindValue(":drawingConfig", dcStr);
        q.bindValue(":id", m_stageId);
        q.exec(qf::core::Exception::Throw);
        getPlugin<EventPlugin>()->clearStageDataCache();
    }
    {
        static const QString qs =
            "UPDATE classdefs SET"
            "  startSlotIndex=:startSlotIndex,"
            "  startTimeMin=:startTimeMin,"
            "  startIntervalMin=:startIntervalMin,"
            "  vacantsBefore=0,"
            "  vacantEvery=0,"
            "  vacantsAfter=:vacantsAfter"
            " WHERE id=:id AND stageId=:stageId";

        qfs::Query q(qfs::Connection::forName());
        q.prepare(qs, qf::core::Exception::Throw);
        for (const ClassInfo &ci : classes) {
            q.bindValue(":startSlotIndex",   ci.startSlotIndex);
            q.bindValue(":startTimeMin",     ci.startTimeMin);
            q.bindValue(":startIntervalMin", ci.assignedInterval);
            q.bindValue(":vacantsAfter",     ci.computedVacantsAfter);
            q.bindValue(":id",               ci.defId);
            q.bindValue(":stageId",          m_stageId);
            q.exec(qf::core::Exception::Throw);
        }
    }
}

bool AutoDrawEngine::run()
{
    QList<ClassInfo> classes = loadClassData();
    if (classes.isEmpty())
        return false;

    AutoDrawAlgorithm algo(m_cfg);
    QList<AutoDrawAlgorithm::CourseGroup> groups = algo.compute(classes);

    saveResult(groups, classes);
    return true;
}

}  // namespace drawing
