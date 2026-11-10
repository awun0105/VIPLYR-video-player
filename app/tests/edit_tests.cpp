#include <QCoreApplication>
#include <cstdio>
#include "EditProject.h"

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char *label) { if (!ok) { ++failures; std::printf("FAILED: %s\n", label); } };
    EditClip a;
    a.id = "a"; a.path = "original-a.mp4"; a.sourceDurationMs = 10000; a.outMs = 10000; a.frameRate = 25;
    EditClip b = a;
    b.id = "b"; b.path = "original-b.mp4"; b.sourceDurationMs = 4000; b.outMs = 4000;
    EditProject project;
    project.clips = {a, b};
    check(project.duration() == 14000 && project.clipAt(10000) == 1, "boundary belongs to next clip");
    check(project.trim(0, 1023, 7017), "trim changes source range");
    check(project.clips[0].inMs == 1040 && project.clips[0].outMs == 7000, "trim snaps to source frames");
    check(project.duration() == 9960 && project.startOf(1) == 5960, "trim ripples following clips");
    check(!project.split(0) && !project.split(5960), "split at boundaries is rejected");
    check(project.split(2000), "split inside a clip");
    check(project.clips.size() == 3 && project.duration() == 9960, "split preserves project duration");
    check(project.clips[0].outMs == project.clips[1].inMs && project.clips[1].path == a.path &&
          project.clips[0].id != project.clips[1].id, "split retains source and creates an independent clip");
    check(project.move(2, 0) && project.clips.first().id == "b", "move clip to front");
    check(project.move(0, 3) && project.clips.last().id == "b", "move clip to end");
    check(!project.move(0, 1) && !project.move(-1, 0), "no-op and invalid moves rejected");
    project.trim(0, -1000, 999999);
    check(project.clips[0].inMs == 0 && project.clips[0].outMs == 10000, "trim remains within original source");
    check(a.inMs == 0 && a.outMs == 10000 && a.path == "original-a.mp4", "original clip value unchanged");
    EditProject tiny;
    a.sourceDurationMs = 10; a.outMs = 10;
    tiny.clips = {a};
    tiny.trim(0, 1000, -1000);
    check(tiny.clips[0].inMs == 0 && tiny.clips[0].outMs == 10, "short sources cannot become empty or invalid");
    std::printf(failures ? "%d FAILURE(S)\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
