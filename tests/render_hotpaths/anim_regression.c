static void put_float(uint8_t *p, float f) { memcpy(p, &f, 4); }
static void put_u32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
int main(void)
{
    uint8_t rec[256] = {0};
    put_u32(rec+8, 2); put_u32(rec+12, 2); put_u32(rec+0x1c, 0x24); put_u32(rec+0x20, 0x60);
    for (int segment=0; segment<2; segment++) {
        for (int i=0; i<3; i++) {
            put_float(rec+0x24+24*segment+4*i, .3f*i+segment);
            put_float(rec+0x24+24*segment+12+4*i, .2f*i);
        }
        for (int i=0; i<4; i++) {
            put_float(rec+0x60+36*segment+4*i, .13f*(i+1)+segment*.01f);
            put_float(rec+0x60+36*segment+16+4*i, .09f*(i+1));
        }
    }
    const uint8_t *tracks[64]; for (int i=0; i<64; i++) tracks[i] = rec;
    Clip c = {.kind=1, .bones=64, .duration=2.5f, .trans=tracks};
    const float angles[] = {0, -0.0f, .237f, -.237f, 1, 3.14f, -3.14f, .000001f, 100.0f};
    const double boundaries[] = {0, -0.0, 1.25, 2.5, -2.5, 5, -5, 1000000,
                                  1.249999999999, 1.250000000001, 2.499999999999, 2.500000000001,
                                  -.000000000001, .000000000001};
    unsigned cases = 0, old_cos = 0, new_cos = 0, old_sin = 0, new_sin = 0;
    for (unsigned a=0; a<sizeof angles/sizeof *angles; a++) {
        for (int segment=0; segment<2; segment++) put_float(rec+0x60+36*segment+32, angles[a]);
        for (int time=0; time<78; time++) for (int bone=0; bone<64; bone++) {
            double seconds = time<64 ? (time-32)*.137 : boundaries[time-64];
            float t[3], q[4], t2[3], q2[4];
            calls_cos = calls_sin = calls_fmod = 0;
            assert(reference_pose(&c, seconds, (uint32_t)bone, t, q));
            assert(calls_cos == (angles[a]==0 ? 0u : 4u) && calls_sin == calls_cos && calls_fmod == 1);
            old_cos += calls_cos; old_sin += calls_sin;
            calls_cos = calls_sin = calls_fmod = 0;
            assert(clip_pose(&c, seconds, (uint32_t)bone, t2, q2));
            assert(calls_cos == (angles[a]==0 ? 0u : 1u) && calls_sin == calls_cos && calls_fmod == 1);
            new_cos += calls_cos; new_sin += calls_sin;
            assert(!memcmp(t, t2, sizeof t) && !memcmp(q, q2, sizeof q)); cases++;
        }
    }
    float t[3] = {1, 2, 3}, q[4] = {1, 2, 3, 4}; calls_cos = calls_sin = calls_fmod = 0;
    assert(!clip_pose(&c, 0, 64, t, q)); c.duration = 0;
    assert(!clip_pose(&c, 0, 0, t, q)); assert(!calls_cos && !calls_sin && !calls_fmod);
    printf("RawAnim: %u bitwise translation/quaternion cases incl zero angle, two segments, negative/boundary times; cos %u->%u sin %u->%u\n",
           cases, old_cos, new_cos, old_sin, new_sin);
    return 0;
}
