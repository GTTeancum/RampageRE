/* Included after exact private Renderer types, allocation, setter and helper. */
#include <assert.h>

static double tick;
static unsigned calls, clock_queries, other_calls;

double win_time(void)
{
    ++clock_queries;
    return tick;
}

static void callback(void) { ++calls; }
static void other_callback(void) { ++other_calls; }

int main(void)
{
    Renderer *r = rnd_create(), *other = rnd_create();
    assert(r && other);
    assert(!r->output_pump && r->output_pump_at == 0);
    r->drawn = 0xdeadbeef;
    r->hud_t = 1234;

    rnd_set_output_pump(NULL, callback);
    tick = 1;
    pump_output(r);
    assert(calls == 0 && clock_queries == 0);

    rnd_set_output_pump(r, callback);
    tick = 0;
    pump_output(r);
    assert(calls == 0);
    tick = .015;
    pump_output(r);
    assert(calls == 0);
    tick = .016;
    pump_output(r);
    assert(calls == 1);
    for (int i = 0; i < 1000; ++i)
        pump_output(r);
    assert(calls == 1);
    tick = .031;
    pump_output(r);
    assert(calls == 1);
    tick = .033;
    pump_output(r);
    assert(calls == 2);

    tick = 2;
    pump_output(r);
    assert(calls == 3);                     /* One service, no catch-up loop. */
    tick = .1;
    pump_output(r);
    assert(calls == 4);                     /* Clock rollback permits service. */
    pump_output(r);
    assert(calls == 4);

    unsigned reads = clock_queries;
    rnd_set_output_pump(r, NULL);
    tick = 4;
    pump_output(r);
    assert(calls == 4 && clock_queries == reads);
    rnd_set_output_pump(r, other_callback);
    pump_output(r);
    assert(other_calls == 1 && calls == 4);
    rnd_set_output_pump(other, callback);
    pump_output(other);
    assert(calls == 5);
    pump_output(r);
    assert(other_calls == 1);
    assert(r->drawn == 0xdeadbeef && r->hud_t == 1234);

    /* No GPU resources were acquired. Renderer teardown is a separate suite. */
    free(other);
    free(r);
    puts("PASS renderer helper: null/disabled, initial time, 16ms threshold, dense calls, "
         "long gap, rollback, rebind, independent renderers, unrelated fields");
    return 0;
}
