# virtual_S0
![virtual_S0 in action](https://github.com/baranator/virtual_S0/blob/master/virtual_s0.jpg?raw=true)
This is a small program running on the ESP8266 that generates an S0-signal based on a wattage-value in a specific MQTT-Topic. 
All necessary parameters such as wifi-credentials, mqtt-broker address and credentials and, topic, the number of Pulses per kWh 
and the flow direction of the wattage to count (in/out/both) can be configured via a IoT-Webconf Webinterface.

The purpose of this program is to optimize the power-output of small photovoltaic-systems in germany. Small photovoltaic-systems built before 01.01.2023 
are not allowed to push more than 70% of their peak-output into the public grid (so called "statische Wirkleistungsbegrenzung").

PV inverters of the brand Fronius have a input pin that can receive a so called S0-signal from a powermeter that tells the inverter how much power is currently consumed
in the household, the inverter is installed in. If there is enough consumption in the household the inverter can effectivly produce more than 70% of its peak-output as
this overage is directly consumed and still only 70% arrive in the public grid. 

Normally to make this work yould need a hardware 3-phase S0-powermeter that costs about 60€. Many people already monitor their current power consumption and have
this value available in their network, e.g. via a smartmeter and a [Volkszaehler](https://www.volkszaehler.org/) . So with this sketch you only need parts for less than 10€ 
and can make use of the "dynamische Wirkleistungsbegrenzung" to produce more renewable energy :) 
