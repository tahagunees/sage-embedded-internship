#include "pc_sim.h"
#include <string.h>

static uint32_t U32(const uint8_t *p)
{
  return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void Put32(uint8_t *p, uint32_t x)
{
  for (unsigned i=0; i<4; ++i) p[i]=(uint8_t)(x>>(8*i));
}
static uint16_t Crc(const uint8_t *p, unsigned n)
{
  uint16_t crc=0xffff;
  while(n--) {
    crc ^= (uint16_t)*p++ << 8;
    for(unsigned i=0;i<8;++i) crc=(uint16_t)((crc<<1)^((crc&0x8000)?0x1021:0));
  }
  return crc;
}
void PCSim_Reset(PCSimState *s) { memset(s,0,sizeof(*s)); }

static int Feedback(PCSimState *s, const uint8_t *p, uint32_t now,
                    const CurrentPiInputs *inputs, uint8_t reverse, uint8_t switched)
{
  uint32_t seq=U32(p), session=U32(p+4);
  uint64_t time=(uint64_t)U32(p+8) | (uint64_t)U32(p+12)<<32;
  uint8_t hall=p[48], sector=p[49], status=p[51];
  int fresh=!s->active || session!=s->session;
  if (!session || !(status&16) || hall>7 || sector>6 ||
      CurrentPi_DecodeHall(hall).sector!=sector || p[50]>3) { ++s->errors; return 0; }
  if (fresh) {
    /* A restarted MCU can attach to an existing PC session at its last ACK. */
    CurrentPi_Init(&s->pi);
    s->session=session; s->active=1; s->stopped=0; s->next_pi_us=time;
  } else {
    if (!s->pending || seq!=s->sequence || time!=s->time_us+1000) return 0;
    if (s->stopped) return 0; /* A new PC session (disconnect/reconnect) is required. */
  }
  s->sequence = (fresh && time==0 && seq==0xffffffffU) ? 0U : seq+1U;
  s->time_us=time; s->last_rx_ms=now; s->status=status;
  s->hall=hall; s->sector=sector; s->encoder=(int32_t)U32(p+40);
  s->pi.fake_current_a=(float)(int32_t)U32(p+28)*0.001f;
  s->pi.real_rpm=(float)(int32_t)U32(p+36)*0.1f;
  s->pi.hall_state=hall; s->pi.commutation=CurrentPi_DecodeHall(hall);
  if (time>=s->next_pi_us) {
    CurrentPiInputs safe=*inputs;
    if (status&4) safe.enabled=0;
    CurrentPi_Control(&s->pi,&safe,s->pi.fake_current_a,s->pi.real_rpm,hall);
    s->next_pi_us=time+10000;
  }
  if (!inputs->enabled || (status&4) || !sector) {
    s->pi.duty_percent=0; s->pi.integral_percent=0;
  }
  s->pi.error_a=s->pi.target_current_a-s->pi.fake_current_a;
  s->pi.elapsed_ms=(uint32_t)(time/1000);
  uint8_t *c=s->command;
  memset(c,0,PC_SIM_COMMAND_SIZE);
  c[0]=0x5a;c[1]=0xa5;c[2]=3;c[3]=0x10;c[4]=22;
  Put32(c+6,s->sequence);Put32(c+10,session);Put32(c+14,1000);
  uint16_t duty=(uint16_t)(s->pi.duty_percent*100.0f+0.5f);
  c[18]=(uint8_t)duty;c[19]=(uint8_t)(duty>>8);c[20]=sector;
  c[21]=(uint8_t)((inputs->enabled && !(status&4) && sector)?1:0) | (reverse?2:0);
  c[22]=switched?1:0;
  Put32(c+24,(uint32_t)(s->pi.target_current_a*1000.0f+0.5f));
  uint16_t crc=Crc(c+2,26);c[28]=(uint8_t)crc;c[29]=(uint8_t)(crc>>8);
  s->pending=1;s->last_tx_ms=now;
  return 1;
}
int PCSim_Feed(PCSimState *s, uint8_t byte, uint32_t now,
               const CurrentPiInputs *inputs, uint8_t reverse, uint8_t switched)
{
  s->buffer[s->used++]=byte;
  while(s->used) {
    uint8_t *b=s->buffer;
    if(b[0]!=0x5a || (s->used>=2 && b[1]!=0xa5)) goto discard;
    if(s->used<6) return 0;
    if(b[2]!=3 || b[3]!=0x11 || b[4]!=60 || b[5]!=0) {++s->errors;goto discard;}
    if(s->used<PC_SIM_FEEDBACK_SIZE) return 0;
    if(Crc(b+2,64)!=((uint16_t)b[66]|(uint16_t)b[67]<<8)) {++s->errors;goto discard;}
    s->used=0;
    return Feedback(s,b+6,now,inputs,reverse,switched);
discard:
    --s->used;memmove(b,b+1,s->used);
  }
  return 0;
}
int PCSim_Retry(PCSimState *s, uint32_t now)
{
  if(!s->pending || s->stopped) return 0;
  if((uint32_t)(now-s->last_rx_ms)>=500) {
    s->stopped=1;s->pi.duty_percent=0;s->pi.integral_percent=0;return 0;
  }
  if((uint32_t)(now-s->last_tx_ms)<50) return 0;
  s->last_tx_ms=now;++s->retries;
  return 1; /* Retransmit EXACT bytes; never advance PI on a retry. */
}
