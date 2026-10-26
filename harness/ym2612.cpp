/* The YM2612 (FM sound) for sound.c: a C interface to ymfm's ym2612 (third_party/ymfm, BSD-3).
 *
 * ymfm leaves the chip's two timers to the host: it says how long a timer will run
 * (ymfm_set_timer) and the host reports when it expires. Sound drivers commonly pace their music
 * with timer A or B, so the timers are counted here in chip clocks, advanced with every sample
 * generated (one sample is 144 clocks). */
#include "ymfm_opn.cpp"                              /* ymfm's OPN chips, compiled here: chip_gains (below) calls
                                                       its FM engine's templates, which only that file completes */
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

/* ymfm's ym2612 with a gain for each FM channel: its generate(), each channel's output weighted
 * (256 = as it is; at 256 for all six the sum is ymfm's own, sample for sample) */
class chip_gains : public ymfm::ym2612 {
public:
    chip_gains(ymfm::ymfm_interface &i) : ymfm::ym2612(i) {}
    void generate_gains(output_data *output, const int *gain)
    {
        m_fm.clock(fm_engine::ALL_CHANNELS);
        output->clear();
        output_data temp;
        int const last_fm_channel = m_dac_enable ? 5 : 6;
        for (int chan = 0; chan < last_fm_channel; chan++) {
            m_fm.output(temp.clear(), 5, 256, 1 << chan);
            int32_t l = dac_discontinuity(temp.data[0]), r = dac_discontinuity(temp.data[1]);
            if (gain[chan] != 256) { l = l * gain[chan] / 256; r = r * gain[chan] / 256; }
            output->data[0] += l; output->data[1] += r;
        }
        if (m_dac_enable) {
            int32_t dacval = dac_discontinuity(int16_t(m_dac_data << 7) >> 7);
            output->data[0] += m_fm.regs().ch_output_0(0x102) ? dacval : dac_discontinuity(0);
            output->data[1] += m_fm.regs().ch_output_1(0x102) ? dacval : dac_discontinuity(0);
        }
        output->data[0] = (output->data[0] * 128) * 64 / (6 * 65);
        output->data[1] = (output->data[1] * 128) * 64 / (6 * 65);
    }
};

host intf;
chip_gains chip(intf);
const int32_t CLOCKS_PER_SAMPLE = 144;

}

extern "C" {

void ym_reset(void) { chip.reset(); intf.remaining[0] = intf.remaining[1] = -1; }
void ym_write(unsigned port, unsigned data) { chip.write(port & 3, (uint8_t)data); }
unsigned ym_read(unsigned port) { return chip.read(port & 3); }

/* one sample of the chip's output, left and right; gain: each FM channel's, 256 as it is */
void ym_generate(int32_t out[2], const int *gain)
{
    ymfm::ym2612::output_data o;
    chip.generate_gains(&o, gain);
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
