# RoboPeak USB Display — Linux kernel driver

Framebuffer plus touchscreen driver for the RoboPeak Mini USB Display
(USB `fccf:a001`). Registers `/dev/fbN` named `rpusbdisp-fb` and an input device
named `RoboPeakUSBDisplayTS`.

## Сборка

```sh
make                                    # против текущего ядра
make KERNEL_SOURCE_DIR=/path/to/kernel  # против другого
sudo insmod rp_usbdisplay.ko
```

## Установка через DKMS

Так модуль пересобирается сам при каждом обновлении ядра:

```sh
sudo ./dkms-install.sh
sudo modprobe rp_usbdisplay
```

Скрипт собирает staging-дерево в `/usr/src/rp-usbdisplay-1.0.0/`, потому что DKMS
копирует один каталог, а общий `protocol.h` лежит вне этого каталога — в
`drivers/common/inc/`. Makefile ищет его в обоих местах, поэтому второй вариант
сборочного файла не нужен.

## Сборка внутри дерева ядра

Для варианта с `Kconfig` см. раздел в корневом [README](../../README.md);
`NewMakefile` предназначен для замены `Makefile` при таком способе.

## Состояние кода

Исходники приведены к современному API ядра и собираются на 6.x и 7.x. Основное,
что менялось относительно оригинала 2013 года: убран `FBINFO_DEFAULT` (удалён из
ядра в 6.10), переход на `screen_buffer`, актуальная сигнатура обработчика
`fb_deferred_io`, восстановление опроса status-эндпоинта после ошибок URB через
workqueue, стиль ядра.

## Известные ограничения

`framebuffer_alloc()` и `input_allocate_device()` вызываются без родительского
устройства, поэтому обе ноды оказываются в `/devices/virtual/` и не приписываются
к seat. Практическое следствие: udev-тег `uaccess` для них не работает, и доступ
без root приходится выдавать через `OWNER=`/`GROUP=` — см.
[`smartscreen/udev/`](../../smartscreen/udev/). Починка — передать
`rpusbdisp_usb_get_devicehandle(dev)` в `framebuffer_alloc()` и выставить
`input_dev->dev.parent`.
