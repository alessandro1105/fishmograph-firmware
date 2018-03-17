# Fishmograph Webapp

Fishmograph is an opensource seismograph based on D7S earthquakes sensor by Omron and Fishino 32. It allow earthquakes monitoring that happen inside our home, notifying us through email of any event registered by the sensor. This allow us to secure people and electronic devices.

This repo contains the firmware for Fishino 32.

## Getting Started

This is a part of the entire Fishmograph project, you can find the webapp code [here](https://github.com/alessandro1105/fishmograph-webapp).

### Installing

You need to update your Fishino 32 to the lastest firmware following the steps that you can find on the Fishino [website](http://fishino.it/home-it.html), remebering to download also the lasted release of the vendor libraris (also in the same website).

This project is based on [D7S](https://github.com/alessandro1105/D7S_Arduino_Library) and [SMTPClient](https://github.com/alessandro1105/SMTPClient_Arduino_Library) libraries, so you need to download and install them too on the Arduino IDE (see [https://www.arduino.cc/en/Guide/Libraries](https://www.arduino.cc/en/Guide/Libraries).).

After preparing the software part of this project, you need to physically connect the D7S sensor to the Fishino 32: INT1 and INT2 pin of the sensor goes to pin 3 and pin 5 of Fishino 32 respectly. Don't forget to connect power supply and I2C bus.

Now we can configure our Fishmograph.

### Configuring

On **INT1_PIN** and **INT2_PIN** you need to enter the Fishino 32 pins on which INT1 and INT2 of the sensor are connected.

**WEBAPP_DEFAULT_PASSWORD** is the default password of the webapp, you can eventually change it inside the webapp itself.

Enail notifications are disabled by default and to enabled them you need to decomment **ENABLE_EMAIL_NOTIFICATION** and fill **SMTP_SERVER and SMTP_PORT** with the correct data of your SMTP server.
Remember: Gmail wants "less secure app" enabled to be used. You can edit this setting on your Google account from [https://myaccount.google.com/lesssecureapps](https://myaccount.google.com/lesssecureapps).

Fill **SMTP_LOGIN** and **SMTP_PASSWD** with your Google account credentials encoded into Base64.

**SMTP_FROM_NAME** e **SMTP_FROM_EMAIL** stand for the sender name and the sender address. If you choose to use Gmail as SMTP server, you have to put your Gmail address into the last one.

Now you can flash Fishmograph firmare into you Fishino 32 and browse to its IP address.

Enjoy!


## Authors

* **Alessandro Pasqualini** - [alessandro1105](https://github.com/alessandro1105)

## Contributors

This project has been developed with the contribution of [Futura Elettronica](http://www.futurashop.it), [Elettronica In](http://www.elettronicain.it), [Open Electronics](https://www.open-electronics.org).

## License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details
