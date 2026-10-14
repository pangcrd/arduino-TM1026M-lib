#pragma once
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI      _bus_instance;
  lgfx::Light_PWM     _light_instance;

public:

  LGFX(void)
  {
    //------------------------------------
    // SPI BUS
    //------------------------------------
    {
      auto cfg = _bus_instance.config();

      cfg.spi_host   = SPI2_HOST;

      cfg.spi_mode   = 0;  //esp32 c3 spimode 3        

      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;

      cfg.spi_3wire  = false;
      cfg.use_lock   = true;

      cfg.dma_channel = SPI_DMA_CH_AUTO;

      cfg.pin_sclk = 12;
      cfg.pin_mosi = 11;
      cfg.pin_miso = 13;
      cfg.pin_dc   = 46;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    //------------------------------------
    // PANEL
    //------------------------------------
    {
      auto cfg = _panel_instance.config();

      cfg.pin_cs   = 10;
      cfg.pin_rst  = -1;
      cfg.pin_busy = -1;

      cfg.memory_width  = 240;
      cfg.memory_height = 320;

      cfg.panel_width   = 240;
      cfg.panel_height  = 320;

      cfg.offset_x = 0;
      cfg.offset_y = 0;

      cfg.offset_rotation = 1;

    
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;

      cfg.readable = false;

      cfg.invert = false;     

      cfg.rgb_order = false;

      cfg.dlen_16bit = false;
      cfg.bus_shared = false;

      _panel_instance.config(cfg);
    }
    {
    auto cfg = _light_instance.config();
    cfg.pin_bl      = 45;      
    cfg.invert      = false; 
    cfg.freq        = 1000;   
    cfg.pwm_channel = 0;      
    _light_instance.config(cfg);
    _panel_instance.setLight(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};

extern LGFX tft;