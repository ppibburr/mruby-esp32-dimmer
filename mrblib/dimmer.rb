module ESP32
  class Dimmer
    def initialize *o
      @data = init *o
    end
    
    def loads &b
      a = []
      for i in 0..n_loads-1
        a << i
        b.call i if b
      end
      a
    end
    
    def disable
      loads.each do |l|
        set_load_power(l, OFF)
      end
      
      reset
    end
    
    def high! ld
      set_load_power ld, 1# ON
    end
    
    def low! ld
      set_load_power ld, 0#OFF
    end
  end
end


