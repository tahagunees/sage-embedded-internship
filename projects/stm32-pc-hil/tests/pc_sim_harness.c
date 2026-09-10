#include "pc_sim.h"
PCSimState test_state;
uint8_t test_rx[68];
CurrentPiInputs test_inputs = CURRENT_PI_DEFAULT_INPUTS;
uint32_t test_now;
uint32_t test_reverse, test_switched;
void test_reset(void) { PCSim_Reset(&test_state);test_now=0; }
int test_feed(void) {
  int result=0;
  for(unsigned i=0;i<68;++i) result+=PCSim_Feed(&test_state,test_rx[i],test_now,&test_inputs,test_reverse!=0,test_switched!=0);
  return result;
}
const uint8_t *test_command(void) { return test_state.command; }
int test_retry(void) { return PCSim_Retry(&test_state,test_now); }
int test_stopped(void) { return test_state.stopped; }
int test_legacy(void) {
  CurrentPiInputs inputs=CURRENT_PI_DEFAULT_INPUTS;
  CurrentPiState s;CurrentPi_Init(&s);
  static const uint8_t sectors[8]={0,1,5,6,3,2,4,0};
  for(unsigned i=0;i<8;++i) if(CurrentPi_DecodeHall(i).sector!=sectors[i]) return 5;
  for(unsigned i=0;i<1000;++i) CurrentPi_Step(&s,&inputs,1500,1);
  if(s.fake_current_a<2.98f || s.fake_current_a>3.02f) return 1;
  if(s.duty_percent<39.7f || s.duty_percent>40.7f) return 2;
  CurrentPi_Step(&s,&inputs,0,0);
  if(s.duty_percent!=0 || s.integral_percent!=0) return 3;
  /* External current must remain unchanged by the controller. */
  CurrentPi_Control(&s,&inputs,2.25f,0,1);
  if(s.fake_current_a!=2.25f) return 4;
  inputs.enabled=0;CurrentPi_Control(&s,&inputs,1,0,1);
  return s.duty_percent!=0;
}
