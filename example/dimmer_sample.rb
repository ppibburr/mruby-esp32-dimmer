d=ESP32::Dimmer.new(18, (12..19).to_a, nil)

v = 0
d.loads.each do |l|
  d.set_load_power l, v+=10
end

puts "ZX Pin: #{d.zx_pin}."
puts "#Loads: #{d.n_loads}."

d.loads do |l|
  puts "Load #{l}: Power #{d.load_power(l)}, Pin #{d.load_pin(l)}."
end



while true;
  ESP32::System.delay 10
  d.loads do |l|
    d.latch l, 1 
  end
end
