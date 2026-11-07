int reference_pose(const Clip *c, double seconds, uint32_t bone, float t[3], float q[4])
{
    if (bone >= c->bones || c->duration <= 0)
        return 0;
    double f = fmod(seconds / c->duration, 1.0);
    if (f < 0) f += 1;
    if (c->kind == 2) {
        float tn = (float)f;
        const uint8_t *tr = c->trans[bone], *keys = tr + 0x10;
        uint32_t n = tr[0xD];
        float scale = f32bits(0x38000100);
        if (n) {
            uint32_t k = 0;
            while (k + 1 < n && (rd16(keys + 8 * (k + 1)) & 0xFFFE) / 65534.0f <= tn) k++;
            float a[3], b[3], fr = 0;
            for (int i = 0; i < 3; i++) a[i] = (int16_t)rd16(keys + 8 * k + 2 + 2 * i) * rdf(tr + 4 * i) * scale;
            if (k + 1 < n) {
                float t0 = (rd16(keys + 8 * k) & 0xFFFE) / 65534.0f, t1 = (rd16(keys + 8 * (k + 1)) & 0xFFFE) / 65534.0f;
                fr = t1 > t0 ? (tn - t0) / (t1 - t0) : 0;
                if (fr < 0) fr = 0;
                for (int i = 0; i < 3; i++) b[i] = (int16_t)rd16(keys + 8 * (k + 1) + 2 + 2 * i) * rdf(tr + 4 * i) * scale;
            } else {
                memcpy(b, a, sizeof b);
            }
            for (int i = 0; i < 3; i++) t[i] = a[i] + (b[i] - a[i]) * fr;
        }
        const uint8_t *rt = c->rot[bone], *rk = rt + 2;
        n = rt[1];
        if (n) {
            uint32_t k = 0;
            while (k + 1 < n && (rd16(rk + 8 * (k + 1)) & 0xFFFE) / 65534.0f <= tn) k++;
            float a[4];
            decode_rotation(rk + 8 * k, a);
            if (k + 1 < n) {
                float b[4], t0 = (rd16(rk + 8 * k) & 0xFFFE) / 65534.0f, t1 = (rd16(rk + 8 * (k + 1)) & 0xFFFE) / 65534.0f;
                float fr = t1 > t0 ? (tn - t0) / (t1 - t0) : 0;
                decode_rotation(rk + 8 * (k + 1), b);
                quat_slerp(a, b, fr < 0 ? 0 : fr, q);
            } else {
                memcpy(q, a, sizeof a);
            }
        }
        return 1;
    }
    const uint8_t *rec = c->trans[bone];
    if (!rec)
        return 0;
    uint32_t nt = rd32(rec + 8), nr = rd32(rec + 12);
    const uint8_t *tp = rec + rd32(rec + 0x1C), *rp = rec + rd32(rec + 0x20);
    double x = f * nt;
    uint32_t k = (uint32_t)x < nt ? (uint32_t)x : nt - 1;
    float fr = (float)(x - k);
    for (int i = 0; i < 3; i++) t[i] = rdf(tp + 24 * k + 4 * i) + rdf(tp + 24 * k + 12 + 4 * i) * fr;
    x = f * nr;
    k = (uint32_t)x < nr ? (uint32_t)x : nr - 1;
    fr = (float)(x - k);
    const uint8_t *s = rp + 36 * k;
    float angle = rdf(s + 32), n = 0;
    float cosine = angle == 0 ? 0 : cosf(angle * fr), sine = angle == 0 ? 0 : sinf(angle * fr);
    for (int i = 0; i < 4; i++) {
        float q0 = rdf(s + 4 * i), q1 = rdf(s + 16 + 4 * i);
        q[i] = angle == 0 ? q0 + q1 * fr : q0 * cosine + q1 * sine;
        n += q[i] * q[i];
    }
    n = sqrtf(n);
    for (int i = 0; i < 4; i++) q[i] /= n;
    return 1;
}
