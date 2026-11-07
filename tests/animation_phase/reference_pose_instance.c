static void old_instance(Scene *s, Instance *in, double t)
{
    int mk = scene_monster_slot(s, (int)(in - s->instances));
    const SceneMonster *sm = mk >= 0 ? &s->mon[mk] : NULL;
    float w = in->held && in->blend_left > 0 && in->blend_time > 0 ? in->blend_left / in->blend_time : 0;
    for (uint32_t i = 0; i < in->tree.n; i++) {
        TreeNode *nd = &in->tree.nodes[i];
        float tr[3] = { nd->t[0], nd->t[1], nd->t[2] }, q[4] = { nd->q[0], nd->q[1], nd->q[2], nd->q[3] };
        if (in->has_clip)
            reference_pose(&in->clip, t + in->offset, i, tr, q);
        if (in->has_face && in->face_mask && in->face_mask[i])   /* the face clip's, fully (weight 1.0) */
            reference_pose(&in->face, in->face_t * in->face.duration, i, tr, q);
        if (nd->parent < 0) {                        /* [R 0x2FFF10] the root's motion comes out of the clip's
                                                      * pose (0x301080; the root rewritten at 0x3003C8) before the
                                                      * blend towards the kept pose, which came out the same way */
            for (int k = 0; k < 3; k++)
                if (in->still_root >> k & 1)
                    tr[k] = in->root_hold[k];
            if (in->root_turn != 0)
                root_unturn(tr, q, in->root_turn);
        }
        float wi = w;                                /* the face bones blend on their own timer (0x2FFF10) */
        if (in->has_face && in->face_mask && in->face_mask[i])
            wi = in->face_held && in->face_blend_left > 0 && in->face_blend_time > 0 ?
                 in->face_blend_left / in->face_blend_time : 0;
        if (wi > 0) {                                /* towards the held pose by the share of the blend left */
            const float *h = in->face_mask && in->face_mask[i] && in->has_face ? in->face_held[i] : in->held[i];
            float w = wi;
            for (int k = 0; k < 3; k++)
                tr[k] += (h[k] - tr[k]) * w;
            quat_slerp(q, h + 3, w > 1 ? 1 : w, q);
        }
        if (in->pose) {
            memcpy(in->pose[i], tr, sizeof tr);
            memcpy(in->pose[i] + 3, q, sizeof q);
        }
        Mat4 local = mat_tq(tr, q);
        for (int k = 0; k < in->nturn; k++)          /* [R 0x298980] local = R x pose (row vectors): R first */
            if (in->turn[k].node == (int)i && in->turn[k].angle != 0) {
                Mat4 r = mat_identity();
                float c = cosf(in->turn[k].angle), sn = sinf(in->turn[k].angle);
                if (in->turn[k].axis == 2) {         /* 0x1015B0: rows (c, s, 0), (-s, c, 0), (0, 0, 1) */
                    r.m[0] = c; r.m[1] = sn; r.m[4] = -sn; r.m[5] = c;
                } else {                             /* 0x101730: rows (1, 0, 0), (0, c, s), (0, -s, c) */
                    r.m[5] = c; r.m[6] = sn; r.m[9] = -sn; r.m[10] = c;
                }
                local = mat_mul(local, r);
            }
        int p = nd->parent;
        in->node[i] = p >= 0 && (uint32_t)p < i ? mat_mul(in->node[p], local) : local;
        if (sm && (sm->look_yaw != 0 || sm->look_pitch != 0)) {
            if ((int)i == sm->look_neck) {           /* model x Rz(yaw) (row vectors): about up, at the neck */
                Mat4 r = mat_trs_z(0, 0, 0, sm->look_yaw * 57.2957795f), m = in->node[i];
                float t[3] = { m.m[12], m.m[13], m.m[14] };
                m.m[12] = m.m[13] = m.m[14] = 0;
                m = mat_mul(r, m);
                m.m[12] = t[0]; m.m[13] = t[1]; m.m[14] = t[2];
                in->node[i] = m;
            } else if ((int)i == sm->look_head)      /* Rz(-pitch) x model (row vectors): about its own z */
                in->node[i] = mat_mul(in->node[i], mat_trs_z(0, 0, 0, -sm->look_pitch * 57.2957795f));
        }
    }
}
