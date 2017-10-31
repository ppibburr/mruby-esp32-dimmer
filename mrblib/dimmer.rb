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
  end
end


