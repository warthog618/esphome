# Fujitsu Legacy

A climate IR remote component for older Fujitsu heatpumps such as the AST12R using the AR-DB1 remote.
The protocol used is a precursor of the fujitsu_general protocol:

- the full state commands use the **0xfc** command code, rather than **0xfe**, and are one byte shorter.
- the utility commands, such as **off**, lack a checksum.
- the **swing** function is not part of the full state command but is a separate command that toggles the swing state on (vertical) and off.

The relevant sections of the ESPHome config including both a transmitter and receiver:

```yaml
climate:
  - platform: fujitsu_legacy
    name: "Fujitsu AC"
    receiver_id: ir_rcvr

remote_receiver:
  id: ir_rcvr
  pin:
    number: GPIOxx
    inverted: true
    mode:
      input: true
      pullup: true

remote_transmitter:
  pin: GPIOyy
  carrier_duty_percent: 50%

external_components:
  - source:
      type: git
      url: https://github.com/warthog618/esphome.git
      ref: fujitsu_legacy
    components: [fujitsu_legacy]
```

Due to the possibility receiving a swing toggle command from the transmitter, the receiver component does not update the swing state and so does not track any swing changes made by the original remote.  To resynchronise, turn the AC off and on again.

Successfully tested with a Fujitsu General AST12RSGCW, but should work with other modles using the AR-DB1 remote, and so the legacy protocol.
