/* The YM2612 (FM sound) for sound.c: a C interface to ymfm's ym2612 (third_party/ymfm, BSD-3).
 *
 * ymfm leaves the chip's two timers to the host: it says how long a timer will run
 * (ymfm_set_timer) and the host reports when it expires. Sound drivers commonly pace their music
 * with timer A or B, so the timers are counted here in chip clocks, advanced with every sample
 * generated (one sample is 144 clocks). */
#include "ymfm_opn.h"
#include <cstring>
#include <cstdio>
#include <vector>

namespace {

class host : public ymfm::ymfm_interface {
public:
    int32_t remaining[2] = {-1, -1};               /* clocks to expiry, -1 when stopped */
    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override { remaining[tnum] = duration_in_clocks; }
    void advance(int32_t clocks)
    {
        for (uint32_t t = 0; t < 2; t++) {
            if (remaining[t] < 0) continue;
            remaining[t] -= clocks;
            if (remaining[t] <= 0) {
                int32_t over = -remaining[t];
                remaining[t] = -1;
                m_engine->engine_timer_expired(t);    /* may set the timer again */
                if (remaining[t] >= 0) remaining[t] -= over;
            }
        }
    }
};

host intf;
ymfm::ym2612 chip(intf);
const int32_t CLOCKS_PER_SAMPLE = 144;

}

extern "C" {

void ym_reset(void) { chip.reset(); intf.remaining[0] = intf.remaining[1] = -1; }
void ym_write(unsigned port, unsigned data) { chip.write(port & 3, (uint8_t)data); }
unsigned ym_read(unsigned port) { return chip.read(port & 3); }

/* one sample of the chip's output, left and right */
void ym_generate(int32_t out[2])
{
    ymfm::ym2612::output_data o;
    chip.generate(&o, 1);
    intf.advance(CLOCKS_PER_SAMPLE);
    out[0] = o.data[0]; out[1] = o.data[1];
}

/* save states: ymfm's own state (its length first), then the timers. state_io is the host's. */
void state_io(FILE *f, void *p, size_t n, int saving);
void ym_state_io(FILE *f, int saving)
{
    std::vector<uint8_t> buf;
    uint32_t n = 0;
    if (saving) {
        ymfm::ymfm_saved_state st(buf, true);
        chip.save_restore(st);
        n = (uint32_t)buf.size();
        state_io(f, &n, sizeof n, 1);
        state_io(f, buf.data(), n, 1);
    } else {
        state_io(f, &n, sizeof n, 0);
        if (n > (1u << 20)) return;                  /* not a state this code wrote */
        buf.resize(n);
        state_io(f, buf.data(), n, 0);
        ymfm::ymfm_saved_state st(buf, false);
        chip.save_restore(st);
    }
    state_io(f, intf.remaining, sizeof intf.remaining, saving);
}

}
