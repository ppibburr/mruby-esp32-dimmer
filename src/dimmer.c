#include <mruby.h>
#include <mruby/value.h>
#include "string.h"
#include "mruby.h"
#include "mruby/irep.h"
#include "mruby/compile.h"
#include "mruby/error.h"
#include "mruby/string.h"
#include "mruby/value.h"
#include "mruby/array.h"
#include "mruby/variable.h"
#include "mruby/data.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define GPIO_MODE_DEF_PULLUP (BIT3)
#define GPIO_MODE_INPUT_PULLUP ((GPIO_MODE_INPUT)|(GPIO_MODE_DEF_PULLUP))

static mrb_value
mrb_esp32_gpio_pin_mode(mrb_state *mrb, mrb_value self) {
  mrb_value pin, dir;

  mrb_get_args(mrb, "oo", &pin, &dir);

  if (!mrb_fixnum_p(pin) || !mrb_fixnum_p(dir)) {
    return mrb_nil_value();
  }

  gpio_pad_select_gpio(mrb_fixnum(pin));
  gpio_set_direction(mrb_fixnum(pin), mrb_fixnum(dir) & ~GPIO_MODE_DEF_PULLUP);

  if (mrb_fixnum(dir) & GPIO_MODE_DEF_PULLUP) {
    gpio_set_pull_mode(mrb_fixnum(pin), GPIO_PULLUP_ONLY);
  }

  return self;
}

typedef struct {
	int pin;
	int power;
} load_t;

typedef struct {
	int zx;
	int gate_hold;
	int n_loads;
	load_t loads[];
} dimmer_t;

int dimmer_get_load_power(dimmer_t* dimmer, int load) {
	return dimmer->loads[load].power;
}

void dimmer_set_load_power(dimmer_t* dimmer, int load, int power) {
	dimmer->loads[load].power = power;
}

int dimmer_get_load_pin(dimmer_t* dimmer, int load) {
	return dimmer->loads[load].pin;
}

int dimmer_get_n_loads(dimmer_t* dimmer) {
	return dimmer->n_loads;
}

load_t* dimmer_get_load_for_pin(dimmer_t* dimmer, int pin) {
	for(int i=0; i < dimmer->n_loads; i++) {
		if (dimmer->loads[i].pin == pin) {
			return &dimmer->loads[i]; 
		}
	}
	
	return NULL;
}


void dimmer_set_channel_low(dimmer_t* dimmer, int l) {
    int pin = dimmer_get_load_pin(dimmer, l);
	// digitalWrite(pin, 0);

    // printf("Pin LOW: %d\n", pin);
}

void dimmer_set_channel_high(dimmer_t* dimmer, int l) {
    int pin = dimmer_get_load_pin(dimmer, l);
	// digitalWrite(pin, 1);
    
    // printf("Pin HIGH: %d\n", pin);	
}

void dimmer_reset(dimmer_t* dimmer) {
	for(int i=0; i < dimmer->n_loads; i++) {
		dimmer_set_channel_low(dimmer, i);
	}
}

void dimmer_update(dimmer_t* dimmer) {
  dimmer_reset(dimmer);
  
  int max_tick = 0;
  
  int l;
  
  for(l=0; l < dimmer->n_loads; l++) {
    if((dimmer->loads[l].power+dimmer->gate_hold) > max_tick) {
		max_tick = (dimmer->loads[l].power+dimmer->gate_hold);
	}
  }
  
  for(int i=0; i <= max_tick; i = i+10) {
	  ets_delay_us(10);
	  
	  // printf("tick %d\n", i);
	  
	  for(l=0; l < dimmer->n_loads; l++) {
		  if (i == dimmer->loads[l].power) {
			if(dimmer_get_load_power(dimmer, l) > 0) {
		      dimmer_set_channel_high(dimmer, l); 
		    }
	      }
      }
      
      for(l=0; l < dimmer->n_loads; l++) {	      
		  if (i == dimmer->loads[l].power+dimmer->gate_hold) {
		    dimmer_set_channel_low(dimmer, l); 
	      }	      
	  }
  }
}

void dimmer_zx_isr_cb(void* data) {
	dimmer_update((dimmer_t*)data);	
}

void dimmer_init_dimmer(dimmer_t* dimmer, int zx_pin, int load_pins[], int n_loads, void* gate_hold) {
	for (int i=0; i < n_loads; i++) {
		load_t load = {0,0};
	    load.pin   = load_pins[i];
	    load.power = 0;
		dimmer->loads[i] = load;
	}
	
	dimmer->zx        = zx_pin;
	dimmer->gate_hold = 10;
	dimmer->n_loads = n_loads;

    if (gate_hold) {
		dimmer->gate_hold = (int)gate_hold;
	}
}


static mrb_value mrb_esp32_dimmer_init(mrb_state* mrb, mrb_value self) {
	mrb_value args, dwell;
	mrb_int zx;
	
	mrb_get_args(mrb, "iAo", &zx,&args,&dwell);
	
	int len = (int)mrb_ary_len(mrb,args);
	int loads[len];
		
	for(int i=0; i < len; i++) {
		loads[i] = mrb_fixnum(mrb_ary_ref(mrb, args, i));
	}
	
	int gate_hold = 10;
	if (mrb_obj_ptr(mrb_nil_value()) != mrb_obj_ptr(dwell)) {
		gate_hold = mrb_fixnum(dwell);
	}
	
	
	dimmer_t* dimmer;
    dimmer = malloc(sizeof(dimmer_t));
    memset(dimmer, 0, sizeof(dimmer_t));
	dimmer_init_dimmer(dimmer, (int)zx, loads, len, gate_hold);
	
	return  mrb_obj_value(Data_Wrap_Struct(mrb, mrb->object_class, NULL, dimmer));
}

void mrb_esp32_dimmer_get_data(mrb_state* mrb, mrb_value iv, void* _t, dimmer_t** dimmer) {
  Data_Get_Struct(mrb, iv, NULL, *dimmer);
}

static mrb_value mrb_esp32_dimmer_get_load_power(mrb_state* mrb, mrb_value self) {
	mrb_int channel;
	mrb_get_args(mrb,"i",&channel);

    dimmer_t* dimmer;
    mrb_value iv = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@data"));
    mrb_esp32_dimmer_get_data(mrb,iv,NULL, &dimmer);
    
    return mrb_fixnum_value(dimmer->loads[(int)channel].power);
}

static mrb_value mrb_esp32_dimmer_set_load_power(mrb_state* mrb, mrb_value self) {
	mrb_int channel, val;
	mrb_get_args(mrb,"ii",&channel, &val);

    dimmer_t* dimmer;
    
    mrb_value iv = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@data"));
    
    mrb_esp32_dimmer_get_data(mrb,iv,NULL, &dimmer);
    
    dimmer->loads[(int)channel].power = (int)val;
    
    return self;
}

static mrb_value mrb_esp32_dimmer_get_zx_pin(mrb_state* mrb, mrb_value self) {
    dimmer_t* dimmer;
    
    mrb_value iv = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@data"));
    
    mrb_esp32_dimmer_get_data(mrb,iv,NULL, &dimmer);
    
    return mrb_fixnum_value(dimmer->zx);
}

static mrb_value mrb_esp32_dimmer_get_n_loads(mrb_state* mrb, mrb_value self) {
    dimmer_t* dimmer;
    
    mrb_value iv = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@data"));
    
    mrb_esp32_dimmer_get_data(mrb,iv,NULL, &dimmer);
    
    return mrb_fixnum_value(dimmer->n_loads);
}

static mrb_value mrb_esp32_dimmer_get_load_pin(mrb_state* mrb, mrb_value self) {
	mrb_int channel;
	
	mrb_get_args(mrb,"i",&channel);
    
    dimmer_t* dimmer;
    
    mrb_value iv = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@data"));
    
    mrb_esp32_dimmer_get_data(mrb,iv,NULL, &dimmer);
    
    return mrb_fixnum_value(dimmer->loads[(int)channel].pin);
}

static mrb_value mrb_esp32_dimmer_reset(mrb_state* mrb, mrb_value self) {
    dimmer_t* dimmer;
    
    mrb_value iv = mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "@data"));
    
    mrb_esp32_dimmer_get_data(mrb,iv,NULL, &dimmer);
    
    dimmer_reset(dimmer);
    
    return self;
}



void
mrb_mruby_esp32_dimmer_gem_init(mrb_state* mrb)
{
  struct RClass *esp32, *dimmer;

  esp32 = mrb_define_module(mrb, "ESP32");

  dimmer = mrb_define_class_under(mrb, esp32, "Dimmer", mrb->object_class);
  mrb_define_method(mrb, dimmer, "init", mrb_esp32_dimmer_init, MRB_ARGS_REQ(3));
  mrb_define_method(mrb, dimmer, "load_power", mrb_esp32_dimmer_get_load_power, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, dimmer, "get_load_power", mrb_esp32_dimmer_get_load_power, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, dimmer, "set_load_power", mrb_esp32_dimmer_set_load_power, MRB_ARGS_REQ(2));  
  mrb_define_method(mrb, dimmer, "reset", mrb_esp32_dimmer_reset, MRB_ARGS_NONE()); 
  mrb_define_method(mrb, dimmer, "n_loads", mrb_esp32_dimmer_get_n_loads, MRB_ARGS_NONE());   
  mrb_define_method(mrb, dimmer, "get_load_pin", mrb_esp32_dimmer_get_load_pin, MRB_ARGS_REQ(1));   
  mrb_define_method(mrb, dimmer, "get_zx_pin", mrb_esp32_dimmer_get_zx_pin, MRB_ARGS_NONE()); 
  mrb_define_method(mrb, dimmer, "load_pin", mrb_esp32_dimmer_get_load_pin, MRB_ARGS_REQ(1));   
  mrb_define_method(mrb, dimmer, "zx_pin", mrb_esp32_dimmer_get_zx_pin, MRB_ARGS_NONE());         
}

void
mrb_mruby_esp32_dimmer_gem_final(mrb_state* mrb)
{
}
