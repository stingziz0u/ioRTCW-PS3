/* ps3_prof.h -- frame profiler report (the per-phase API is in ps3_platform.h) */

#ifndef PS3_PROF_H
#define PS3_PROF_H

/* one vertex ring segment: PS3GL_VRING_IO_SIZE / PS3GL_VRING_SEGMENTS (ps3gl.h) */
#define PS3GL_VRING_SEGMENT_KB  2048

#define PS3_PROF_MAX_SYSCALLS   1024

void PS3_ProfFrameEnd(double frame_us);
void PS3_ProfReport(void);
void PS3_ProfReset(void);

#endif
