/* Asset-free runner tests, included after the fixture allocator and API.
 * Both runners share current transition/input/event logic; the reference uses
 * legacy_windows.c's old allocation strategy only. */
typedef struct {
    unsigned calls;
    uint64_t hash;
} RunnerCalls;

static int runner_call(void *ctx, const char *name, const float *args)
{
    RunnerCalls *calls = ctx;
    const unsigned char *bytes = (const unsigned char *)args;
    for (; *name; name++) calls->hash = calls->hash * 131 + (unsigned char)*name;
    for (size_t i = 0; i < 4 * sizeof(float); i++) calls->hash = calls->hash * 131 + bytes[i];
    calls->calls++;
    return calls->calls % 3 != 0;
}

static void runner_equal(const MoveRunner *a, const MoveRunner *b)
{
    assert(a->g == b->g && a->state == b->state && !memcmp(&a->time, &b->time, sizeof a->time));
    assert(a->chosen == b->chosen && a->from == b->from && a->via == b->via);
    assert(a->floor_any == b->floor_any && a->floor_act == b->floor_act);
    int n = a->g->states[a->state].ntrans;
    if (n) {
        assert(!memcmp(a->in_window, b->in_window, (size_t)n));
        assert(!memcmp(a->sw_window, b->sw_window, (size_t)n));
    }
}

static void runner_input(MoveInput *in, unsigned seed, int candidate)
{
    memset(in, 0, sizeof *in);
    in->mirrored = seed & 1;
    for (int k = 0; k < MV_COUNT; k++) {
        in->v[k] = ((int)((seed + (unsigned)k) % 9) - 4) * 0.25f;
        in->fresh[k] = ((int)((seed + (unsigned)k) % 5) - 1) * 0.1f;
    }
    if (candidate) {
        c_derive(in);
        c_sectors(in, in->v[MV_LX], -in->v[MV_LY]);
    } else {
        b_derive(in);
        b_sectors(in, in->v[MV_LX], -in->v[MV_LY]);
    }
}

static void runner_tests(void)
{
    assert(!nlive);
    attempts = fail_window = failure_seen = 0;
    memset(allocations_by_domain, 0, sizeof allocations_by_domain);
    MoveEvent events[2] = {{.at = 0.1f, .type = 7}, {.at = 0.7f, .type = 6}};
    MoveTrans transitions[6] = {
        {.target = 1, .flags = 0x11, .in0 = 0, .in1 = 0.6f, .sw0 = 0.2f, .sw1 = 0.9f,
         .priority = 2, .ncond = 1, .cond = {{4, MV_TRUE, 1, 1}}, .call = "gate", .args = {1, 2, 3, 4}},
        {.target = 2, .flags = 0x10, .in0 = 0.5f, .in1 = 0.8f, .sw0 = 0.5f, .sw1 = 0.9f,
         .priority = 3, .ncond = 1, .cond = {{0, MV_M_LEFT, 3, 0}}},
        {.target = 3, .flags = 0x10, .in0 = 0, .in1 = 1, .sw0 = 0, .sw1 = 1,
         .priority = 1, .ncond = 1, .cond = {{4, MV_FALSE, 1, 0}}},
        {.target = 0, .flags = 0x11, .in0 = 0, .in1 = 1, .sw0 = 0.4f, .sw1 = 1.5f,
         .priority = 2, .ncond = 1, .cond = {{4, MV_TRUE, 1, 1}}},
        {.target = 2, .flags = 0x10, .in0 = 0.2f, .in1 = 0.3f, .sw0 = 0.2f, .sw1 = 0.3f,
         .priority = 3, .ncond = 1, .cond = {{4, MV_SQUARE, 0, 1}}},
        {.target = 3, .flags = 0x10, .in0 = 0, .in1 = 0.3f, .sw0 = 0, .sw1 = 0.3f,
         .priority = 1, .ncond = 1, .cond = {{4, MV_TRUE, 1, 1}}}
    };
    MoveState states[4] = {
        {.name = "A", .length = 1, .next = 0, .trans = transitions, .ntrans = 3, .events = events, .nevents = 2},
        {.name = "B", .length = 2, .next = 2, .trans = transitions + 3, .ntrans = 2, .events = events, .nevents = 2},
        {.name = "C", .length = 0.125f, .next = 0, .events = events, .nevents = 2},
        {.name = "D", .length = 0, .next = 3, .trans = transitions + 5, .ntrans = 1, .events = events, .nevents = 2}
    };
    MoveGraph graph = {.states = states, .n = 4};
    MoveRunner a, b;
    b_start(&a, &graph, -1);
    assert(!c_start(&b, &graph, -1));
    unsigned compared = 0, event_switches = 0, callback_count = 0;
    const float steps[] = {-0.1f, 0, 1.0f / 60, 0.25f, 1.25f};
    for (int state = 0; state < 4; state++) {
        for (unsigned seed = 0; seed < 16; seed++) {
            b_force(&a, state);
            c_force(&b, state);
            runner_equal(&a, &b);
            for (unsigned step = 0; step < 50; step++) {
                MoveInput ia, ib;
                runner_input(&ia, seed + step, 0);
                runner_input(&ib, seed + step, 1);
                assert(!memcmp(&ia, &ib, sizeof ia));
                RunnerCalls ca = {0}, cb = {0};
                const MoveState *old = &states[a.state];
                float from = a.time;
                int ra = b_step(&a, &ia, steps[step % 5], runner_call, &ca);
                int rb = c_step(&b, &ib, steps[step % 5], runner_call, &cb);
                assert(ra == rb && ca.calls == cb.calls && ca.hash == cb.hash);
                assert(!memcmp(&ia, &ib, sizeof ia));
                runner_equal(&a, &b);
                const MoveEvent *ea, *eb;
                assert(b_events_between(old, from, a.time, &ea) == c_events_between(old, from, b.time, &eb));
                assert(ea == eb);
                callback_count += ca.calls;
                compared++;
            }
        }
    }
    b_force(&a, 0);
    c_force(&b, 0);
    assert(!b_event(&a, "D") && !c_event(&b, "D"));
    runner_equal(&a, &b);
    event_switches++;
    assert(b_event(&a, "missing") == -1 && c_event(&b, "missing") == -1);
    assert(allocations_by_domain[2] == 1 && allocations_by_domain[1] > 2);
    b_stop(&a);
    c_stop(&b);
    assert(!nlive);

    /* First and second legacy allocations independently publish a partial pair. */
    for (int failure = 1; failure <= 2; failure++) {
        fail_window = attempts + failure;
        b_start(&a, &graph, 0);
        assert((failure == 1 && !a.in_window && a.sw_window) ||
               (failure == 2 && a.in_window && !a.sw_window));
        b_stop(&a);
        assert(!nlive);
    }
    fail_window = attempts + 1;
    assert(c_start(&b, &graph, 0) == -1);
    MoveRunner empty = {0};
    assert(!memcmp(&b, &empty, sizeof b));
    c_stop(&b);
    assert(!nlive);
    fail_window = 0;
    assert(!c_start(&b, &graph, 0));
    int warmed = attempts;
    fail_window = attempts + 1;
    for (int i = 0; i < 1000; i++) c_force(&b, i % 4);
    assert(attempts == warmed);
    c_stop(&b);
    c_stop(&b);
    assert(!nlive);

    MoveState zero = {.length = 1};
    MoveGraph zero_graph = {.states = &zero, .n = 1};
    assert(!c_start(&b, &zero_graph, 0) && !b.in_window && !b.sw_window);
    MoveInput input = {0};
    assert(c_step(&b, &input, 2, NULL, NULL) == 1 && attempts == warmed);
    c_stop(&b);
    fail_window = 0;
    MoveTrans many[9] = {{0}};
    MoveState large = {.length = 1, .ntrans = 9, .trans = many};
    MoveGraph larger = {.states = &large, .n = 1};
    assert(!c_start(&a, &larger, 0));
    assert(!c_start(&b, &graph, graph.n) && b.state == 0);
    assert(counts[WINDOW] == 2);
    c_stop(&a);
    assert(counts[WINDOW] == 1);
    c_force(&b, 1);
    c_stop(&b);
    assert(!nlive);
    assert(c_start(&b, NULL, 0) == -1);
    MoveGraph invalid = {0};
    assert(c_start(&b, &invalid, 0) == -1);
    zero.ntrans = -1;
    assert(c_start(&b, &zero_graph, 0) == -1);
    c_stop(&b);
    assert(!nlive);
    printf("PASS %u synthetic runner comparisons, %u event switch, %u callbacks; "
           "OOM retry, zero transitions, 1000 warm entries, graph replacement and independent teardown\n",
           compared, event_switches, callback_count);
}
