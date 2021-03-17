#!/usr/bin/env python3

"""
 * nxdt_host.pyw
 *
 * Copyright (c) 2021, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * nxdumptool is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""

# This script depends on Pillow, PyUSB and tqdm. You can install them with `pip install Pillow pyusb tqdm`.

# libusb needs to be installed as well. PyUSB uses it as its USB backend. Otherwise, a NoBackend exception will be raised while calling PyUSB functions.
# Under Windows, the recommended way to do this is by installing the libusb driver with Zadig (https://zadig.akeo.ie). This is a common step in Switch modding guides.
# Under MacOS, use `brew install libusb` to install libusb via Homebrew.
# Under Linux, you should be good to go from the start. If not, just use the package manager from your distro to install libusb.

# Optionally, comtypes may also be installed under Windows to provide taskbar progress functionality. You can install it with `pip install comtypes`.

import os
import platform
import threading
import traceback
import ctypes
import logging
import queue
import shutil
import time
import struct
import usb.core
import usb.util
import warnings

import tkinter as tk
import tkinter.ttk as ttk
from tkinter import filedialog, messagebox, font, scrolledtext

from tqdm import tqdm

import base64
import io
from PIL import Image, ImageTk

# Scaling factors.
WINDOWS_SCALING_FACTOR = 96.0
SCALE = 1.0

# Window size.
WINDOW_WIDTH  = 500
WINDOW_HEIGHT = 470

# Application version.
APP_VERSION = '0.2'

# Copyright year.
COPYRIGHT_YEAR = '2021'

# Messages displayed as labels.
SERVER_START_MSG = 'Please connect a Nintendo Switch console running nxdumptool.'
SERVER_STOP_MSG = 'Exit nxdumptool on your console or disconnect it at any time to stop the server.'

# USB VID/PID pair.
USB_DEV_VID = 0x057E
USB_DEV_PID = 0x3000

# USB manufacturer and product strings.
USB_DEV_MANUFACTURER = 'DarkMatterCore'
USB_DEV_PRODUCT = 'nxdumptool'

# USB timeout (milliseconds).
USB_TRANSFER_TIMEOUT = 5000

# USB transfer block size.
USB_TRANSFER_BLOCK_SIZE = 0x800000

# USB transfer threshold. Used to determine whether a progress bar should be displayed or not.
USB_TRANSFER_THRESHOLD = round(float(USB_TRANSFER_BLOCK_SIZE) * 2.5)

# USB command header/status magic word.
USB_MAGIC_WORD = b'NXDT'

# Supported USB ABI version.
USB_ABI_VERSION = 1

# USB command header size.
USB_CMD_HEADER_SIZE = 0x10

# USB command IDs.
USB_CMD_START_SESSION        = 0
USB_CMD_SEND_FILE_PROPERTIES = 1
USB_CMD_CANCEL_FILE_TRANSFER = 2
USB_CMD_SEND_NSP_HEADER      = 3
USB_CMD_END_SESSION          = 4

# USB command block sizes.
USB_CMD_BLOCK_SIZE_START_SESSION        = 0x10
USB_CMD_BLOCK_SIZE_SEND_FILE_PROPERTIES = 0x320

# Max filename length (file properties).
USB_FILE_PROPERTIES_MAX_NAME_LENGTH = 0x300

# USB status codes.
USB_STATUS_SUCCESS                 = 0
USB_STATUS_INVALID_MAGIC_WORD      = 4
USB_STATUS_UNSUPPORTED_CMD         = 5
USB_STATUS_UNSUPPORTED_ABI_VERSION = 6
USB_STATUS_MALFORMED_CMD           = 7
USB_STATUS_HOST_IO_ERROR           = 8

# Default directory paths.
INITIAL_DIR = os.path.abspath(os.path.dirname(__file__))
DEFAULT_DIR = (INITIAL_DIR + os.path.sep + USB_DEV_PRODUCT)

# Application icon.
APP_ICON = b'iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAYAAABXAvmHAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAAEnQAABJ0Ad5mH3gAABfVelRYdFJhdyBw' + \
           b'cm9maWxlIHR5cGUgZXhpZgAAeNrNmll23TiWRf8xihoC+mY4uGjWqhnU8GsfPjV2SJGpcuRHWbYoU3wkcJvTAHTnf/77uv/iT83Ru1xar6NWz5888oiTH7p//RnP9+Dz' + \
           b'8/39T3j7/tt59/Fj5Jg4ptcv2nz71OR8+fzA+zOC/X7e9bffxP52o/cnx9ch6cn6ef06SM7H1/mQ32403i6oo7dfh2pvH1jHfw7l7V/9fZLP/92vJ3IjSrvwoBTjSSF5' + \
           b'vnc9PTGyNNLkWPkeUos6E54zTd9dqm/TUJS/C2r6m/Pe/xq04H6Skb9LSMrvD9ONfg1w/TiG786H8pfz6eMx8bcRpfnx5Pjr+Z1C+zKdt3/37n7vUbCZxcyVMNe3Sb1P' + \
           b'5fmJC0lgTs/HKl+Nf4Wf2/M1+Op++uWog02NGF8rjBBJ1Q057DDDDec5rrAYYo4nkqsY44rpOdfJ3YjryWh2Kaccbmwkd6dOohcpT5yNH2MJz3PH87gVOg/egStj4GaB' + \
           b'Tzxf7v2Hf/r17Y3uVbmH4PtHrBhXfKotKIpJ37mKhIT7XkflCfD711//qJ8SGSxPmDsTnN5et7ASPmsruSfRiQsLx1cDhrbfbkCIeHZhMLRDDr6GVEINvsXYQiCOnfxM' + \
           b'Rh6Ty9FIQSglbkYZc0qV5PSoZ/OZFp5rY4mv0+AViSh0XCM1dB+5yrnk6mjUTg3NkkoupdTSSi+jzJpqrqXW2qqAb7bUciutttZ6G2321HMvvfbWex99uhFHAhjLqKON' + \
           b'PsaYk4dO7jz59OyTExYtWbZi1Zp1GzYX5bPyKquutvoaa7odd9pgx6677b7HniccSunkU0497fQzzryU2k0333LrbbffcedH1p6sut9y9jVz/zpr4S1rJMw9Octc9J41' + \
           b'Trf2fosgOCnKGRmLOZDxpgxQ0FE58z3kHJU5p5z5EemKEhllUXJ2UMbIYD4hlhs+cveZuS95c/T9n+Yt/po5p9T9JzLnlLpvMvc1b99kbc+HpoQPAJvaUEH1ifa7+czY' + \
           b'+ev9x3H1G/ztva67kjHDsvayWhbBy9nftc4+3aW76o+u1IW2opW567ypnJ3LYZxU2GqdYIfbitqUKJc4WzzdGhNdZvGMGrkfM4/ReNiu3MbWbpRQs23ExkDRQrqyK5fq' + \
           b'GNw9ELW9b0nH1M8rEm+qIfdnijODD4GrKLVfjzBMBct3dORT/xlhmPkz7jq91rPSbDdNgs9wZ1p2bmagJ9+U1tqcPmuOthJRFwrO6HicTfD+7cFSEz873vn2kbUqc3J0' + \
           b'aND0x+2t5VOpkFGXjRNj7+GOsW17Er7/zYXu80pCr2Buisf27LYpo3RKvx7wOZTFndTCnIX62oECm2O0WnakKm04qhfRxtOmctLGvJWP0AIUXYWbmLZx19Op7Kk49G12' + \
           b'5rrz9IxK6bOvUgaU/RTeAJHzKwu/HX0Y0LjRPrvr5/yZiM/sDCt707SdqjJqZd1BRkhLK/tanOX6e9q9Lz2n51GVq/ovpf8cnf+bX3wc05k/aRP3Xv3/tE2c+uSzTbhB' + \
           b'nmPZ1EwL/cKPBWBpKVvuE4hNP+i1pii3YK3tMm7ZvucK44UOGL3lgpr5tkABf+RbqiOSAY8G6ZR/b5kgw5Sg86YHdqpGBVCR5bZZbKBdxjjkz+zWku2Eu65DbL26kBG0' + \
           b'vr4tgp8c3dsPdf5a1UlVvegBlfVUWQN5/r0k16znMrApfRS6CDtOoPYNFpjvdwUAlm5UJAB+ln6On1P2n3HY0/G0lOmIDYqdtpg7AbB57h7wU6eGVZL9bQ4th5O+BSb3' + \
           b'1xM/Od5nyLYGdFC+r+yfTuTrPNyfTeTr0f2TCcENVsdEd08U2+phk7qn8vK8hj4Y9LiBGWf1Lp7dZ52DuB4yRGE37ibwLPmBfku75+lmjiWcCs/2fxmU/VtQrP41uRDk' + \
           b'/ZOQfDm6/1toApNl1IiMvgwj4dVZuwtqQR1wdJ0oz7LvyAfd0wDGdK7EDdzFdOvtGIuoqgAAim9gRWX46epfAOgdYukEwlnH1e/iruf4QVMD0fthcaReSUAwxWIQyJi9' + \
           b'nGCWIBSQcKV8Q53DVfgDzoGnz9oM/sbUBelBLJ9glDNigFPnFnLkrolw435yOwUABUl72ee4dOoGtxaCaujBt4uRMvS3dkX77aSB2NiIwFysIysqQqs1/Pqeq0UY+5wc' + \
           b'HMKjA8fINcEnVTGZy9DFgHUHGAnOOAHXjnXAZa3rsWQL/XhOAwtxZ7Bm2q6dm2Lp6NJSEl1T850r7F0e+bLvRNj1xfnhqzIycJFhj14zBDwQZgD6yODRoS7psMmnSGXr' + \
           b'zONSuIVRoDlDH6HXgOC5dq34sZ+qGz7XFjP3mQe9AgOag/lHhkAzGmk1oRF6B0Q/+0bIiRhtOzXcWVAM6CViGFaJi3FssDQfwDJRDO6SROgv1dY3pUJPVLQW/BHFxDFT' + \
           b'FCSU6OeabCSKg8YhZYHMCimWZMk800liU5GIll0mVUOg8uGz+41vZkr/joufo/t3F/xyDIQEVbN7RIGAErDZhsnmJjj4tVAHoAKy3DjlCONaje6n1FCgy/qizcmHwbKl' + \
           b'9u017IFkbTO2FTdKQy4HT9u2SWkiF6Xi4evmUST+pEtFp9uVz+bJDtU+JPn8IbeT6hkDYbWl/zdCi4SQBGuw56EzubB6qUqgJlNp5IEoIijwAbgd4nxoLPR/v7PVSRmj' + \
           b'NAa07IA80rV6aj7QbSiDsrzwC90ASF7o3zMVWrv2ZAwz0QRtoblb4IqIfWmI3OloR7R5u63FgK67OW1GoaSOG7Kwr/Br8xLoBCoTNkqBwttIQL+mH3CIvxfwj0sq8xaP' + \
           b'FqLb8GfoklFvjNJKt1KafHpg1blvkqq+fL5fBHp+KolQxIZd9yrIPPuhIciunPlmVlDXuWAyGZFgz9YJ+k7x1DvAfpQYTMCH8RuNoJA18QGKxi4Sad5JyxSjaSe4nipR' + \
           b'1lO5e0HagJ6cw/oh0XuPz5MbFyLiXNmEftWG76BZCCMG8cI9eINwOwYjQEu4wYk63ATDgE84iA4qjJiaKwfrkoaLFBtJQNEpw2Oh5TKmwx8qEWRBbKsIJqXSTEonebSv' + \
           b'R5XndajiMlKmEK27cG88F30UPEzBM+pTKRUYD4DexZM3Ve65FX0W8buUptWMNNpd+OaZKNDnPBVtOHDw5+4GHOOCLOxAY92GgaVUDl71FLKHuGAuC7wMydoITBnVT+3h' + \
           b'bTHH6PguRgU2SHCaK+HfIoS+xWsgSIJUguQr1WUZsKVVAbZkYHfBOFPCqboAThIgsMSgGTh+xAabkLXUAfDrFwOqeKCQt6cIKI57wKyCOgX1ib4NLQ65FZj+TuS9gNZG' + \
           b'Z6GSMT8L1ouHpEfoKI+OaKcMQDk6JE+D+0B244Ncl3NojsqYTdOcUESlxZCk0B74OpIWBJDdJAuvDiWiST2EunuooD7lAAJM0o+5Ks4qiFLMQ1no45xwrzZHb6hbUBx5' + \
           b'E0icdYGY9ItWO0BemNQglVXglKSlCPTRpOguHExiNfjUrnAdnXQaDo5fbH4/PbSCfircm3DVI9eMmmYqF4dAhTvanYDSFIQZuEM5FNrbqH+jFcLssNKsfU2g1XhWGL7h' + \
           b'LyYTpf6CFTzhRp041NauBDjqRKHsaQmKESINEXdxMPHU7emeQpThJzKcITYwHALsosC0CD4dfQ77+cJjgbPr8fmRokXZ0M0t91tTMcRCYjB0SMbwRICQukI0IHciaNdW' + \
           b'XQ50YIq5IjbgYUxqU/P4HQEoPpPxtlCvp50MRUdpGp4KvTglGrw/EfNp8IJ7QALmpUymske1IbDAFEudWq/6fWPMOmatUaEcICN8GdkxmpZJ0kfmEAGkkKobsDTgnivm' + \
           b'F672xwt36RoyV7GXi67ONF1H+EG0j/ajRMA4arVEB1YcCjVolbkibUGfTW2UYyPuyCAL0gHaS5uETV1Jrsamnce0chfOv6ZUusMyQHONhxJciLK1oSUlGgFWobmQOSiI' + \
           b'Z0YL2YaKo53KAS/3Isf0/MjEgRuRSsAR78iATWtSjAFlbWQ7JxwJvNVoVJqdXjmRHxNaYx30REoEGl23kIH0mjGFxs8vddgRLDuiqORcDr0P2oFFpNBqLEV+k6Az84Iw' + \
           b'qrTODVRUVR35i+gKQQKNauPBsGOIeF2ESUAeqiAgzRthKi8hO1A1XJgDgIXvwGpjI9zwozDG9YJoxi3FhQwe4BETooZMGoNjB6wj14FWjIl0zE1+LkXYpt+uS2DDzyCp' + \
           b'BHQ2CLNTgGvtsJKnTWk2OKzS5bEJ4yRYOy5FEtuXkqX7wSNkMLqRukJwYr6to1smZZWIIVlLgBXOgZyhFCit8vi5LEwE5PHrdBz22LwbmAuLmCF0Bd2HGhLNwa1oOzPA' + \
           b'AL3AkAvsOFpEADFuFLryfFLsRJc6rdRRm91nHFWbyFkEn0Q/WSSihZ5c9HeEZS5qOlBHNPRRELRUbwiPvQ/FXevMDkoryGPGVVKBlMh3BZsq6ECv7v4wJEAIHcJMRV1h' + \
           b'whdkXX4rUwxDHA52JiCBQkO0T1NRe3wdIsc3gKNoHVInrjZdKD2IePbmVz/Q0FAyyR/SzdkFWp51WuopwYAbpa2soM8zKHuwQQP48khC5BHyo05MD7/zOJUXROI+DE+L' + \
           b'GUC1bGq70fkBLV5F9VRXw+JBRwjPY8eeqgxDNIxCpxi9bjFrEQVPBz1pYbnhNbBwkEWmRWbW8jNXaChwFyo9KbIkNns0eQN8vNrMoGywm7Q7f7XsTFLhnXQAXijswVKo' + \
           b'OqCdRa8dK07DD7nIXbX4hrCF5R+Jh5hRQzioGXEskYZI9MpLRCBSjjycWsInimvTQ4hYr4HfxI3sdmgowhE6HUK8vYvLNr8Js6JYFWnpraA9MooUNaYOSowaxTk2+HIj' + \
           b'ovQsu3sa3H8GrThxAu6Su5Oz1kw9jIdYKXL2iDcgI+AX0ddJ0g0fhI1AHgHWonuwQ1IoyNkSEEdjw5fVZvWIniNYikPVcX30yOZF3zbgH6NxshbwRfUTgsNCkBtbFf8E' + \
           b'qjvicvSfBXUzFqwqg45APc2O9MWN02SdSyYNh97a5Rw8AyYDp4l41lJFKLc7KH1p0bOZkjC1NCyPTiljSxDQuC64E8YlMnnD9xC874unEGIEAlJMmxUbMUr90EY0iRoV' + \
           b'54lJApPQeBIxSAhYFgFidN+QfB705aEe0C+oKDwWmqqm4Dq2OXuECxI/ILIT2hiA0pC1SntUj/Ghydx3RnEZyj/SnfdZrQhoaa+iBEYO6BCyVP14VNmcWr0xUrsgfGAv' + \
           b'qWG3dpI6bg4MwBcDLhQHMKkpJ7yG5PE5D+5RHUSSaMp3UPekMi9jeFvrHFE6Q1wfI7XlzRaqGRmHPo1cPxxsg+KaA/latI1UaT0ZWbhsUvxRq6EobCRI6xQ4JRAI05Uf' + \
           b'fcwF2g3fMreEFj0LUiBYcEWlJG1Oz0oP+TXSM9khT07ORsDDwADXJOaWdib2QzyYaSe3HWgROgxOpOfAJxIOAUMh+Hy+QT7IEmRNP9qho0iWCF4r11zP7eGr6yDRABcB' + \
           b'wQXkRyox1wGPAuxaadH+QEJndoppyrwXif5UZPu0IE0zVEvMHYTsFDx5B5NuQKtHesF8x2t5OC5qlw25VWlumahrxG/no94zqU1MlKdXugPpE/EN+OawZEqwIQEhPUSA' + \
           b'EVKh1XAQoA7YjLzARWGAeLQWl5ggN7zy2s5j0agphBRlov13v6SdUZGhaLOJUWpdGXu+pX33kjcNezEJiAUBSZ1V+sEF2mYtVAXSAJdVkp8IgSM0RqZy540SJInae57i' + \
           b'TPJpSHGpDa5BtWGCKUwXEbMVPh0JhYzhN2RtG+gSxAP+GhMCfUl20iJ005otjPOst8EOSDp69aFUNKRJEjf4BYGCZpYW0j4pKgJMXEGbbSAaMS4H2GuPP1jcfenFgaa9' + \
           b'V/omOd+11IdsGP0pPuxDHjiybmpzWqXJ6SNdNbtIjSWU8VLbo2hIqKmX/GyqI5TFJYpwlRZOcceQe4Nj5ChE4ScxQ7UOpUuNxFioDRgZNVPhcvoWXHJKTEe70+jP6iIg' + \
           b'21Bqviw18KKUpJuRFquQVKFu5M6UbaJ4gwAfwwKAO6l0uAtOBLqIUp8FxdSaXq/IWh0iGRQAiIizFqbyQKkbSATugLHuBfnA7EY6MHQHll9UbkQxou40ZqgVG6y4FRSx' + \
           b'jqhVuvcBOTwpNHG1OoSEDjStTptvewF1oDCJb5Q6g1gx4rYORifQu2BWk/1RiC1ECIDMhag1fWzAqtFhFxAhSytCdEUFGmA0LEWHgZ8lOgPBtQ8dCQlKh8pZXpvOOd6L' + \
           b'Yyf/0Em77lJS2E65gOcXSS6Ypq0zYXO0pa1Wlrs2wAfLNPCbOOkQNwSBK26+T/pZLzI1D4ZqbVrbWaiUIKB5xC+dtkA4mK61OPDRMslgEPmTlOtawQcYlEMnytQDKqSE' + \
           b'SIGFE22svSQi+9appmXluiTlPK4Wa6i3UagwkDktWRaPF0GBEUF6flBy2Lbt4VRtr09k/fTIjw2yUlkIaGCSvuszArCI9Ug4CBKYKxGxKInHw/Mx7dHDUWAGwD2f0KoM' + \
           b'B8UfB+bHgLSkRTrUXAEHAWDRGU0XyJp02GsVFz0SI1aip0PrE7dx2ypPTU2PipiNppt30T5oF0pXy0j1VCJSgRGAyHetNLe5qBaMDJHHb1+i3xHS0lX75Rtk3g0EEypj' + \
           b'wLTnTr9rZXS5Sq7nAFOBfWTWOgQFQfKsNaSL8sS7R62SNWhMIhqqQ260pReiaFPGxCgBNngBcQwc0Wp6wUpmXHI9CW0QjIZbozLG0JYxrA6nlO4PGIor5bkUpMJRHMar' + \
           b'UM21avkLHwPiZE5UZDPWf+NHSQK1GJ4WQFNsyXLUyx4YDENi6lWQVR2+qWgLH76EQIEphBMoCjpA82BBGD3Za+EMFXfQrkQE7/KAICC8NkhFC1GQfDBhxOhJ7bTAfk2M' + \
           b'AkWngULHJwKdNUDxwM9M4CT2JUlaAzviFWw/Bt9BcohF2PhEkBJ4hNVkZJFqGJeolRjQtjBOocbAZuFui/5uVBzapqjZ4nEVB4jjLDjDo+0H7CmOg6AHDWde6cW26MCI' + \
           b'5IroeOygBBcy/NrQsqw2Vey45dPUFLRGz3MDtmODKuA/kgEhDv3R21o26ZjvHEEuvfSFuiooOMRGQJchwt0Vl/WREQC54fz6JX5p0ktau9lEJWp3bdF6cDTF25t28SqQ' + \
           b'gZ/V4maU99muDS1mapntWZmOaraVaxQ1HoBlPIU3uCGwcwXzExm9MVEY6axlHJXgCDTtQrDlV2sapD+W5vpsnT0LgBnLBlPlCDY8eyjkFYkADBdGBsC3560lB4cXxOXB' + \
           b'ljKiBLH70YuW6+D1bWDmlJdGGhJrICRBHAD1xJ17YQO+RcuI5pAKBhQSjCz+lNOAmpIQZAXCUGuGBBlyZZ79WQwJqDJkEc2ADpVz9Xh31zFVoCoEQGFi9lAJIF/TksIi' + \
           b'LsEip8Jr9xbN1bTvFTTMRg4x1nD2I1od8HXAeAl9ujLPbTXr7UcsbIN2NTKtPaeNvQcJDQGpFT1llda+IT3r+H08vDbpouGxCbTxiYhO2FLqFv+2ABB0GXqj4GwpKTyj' + \
           b'gKGhtWjHpQjI8yQXtapWtcun3QK9B1YfXkdLzqyl0qM3hgJPuZTT1ittvWoPsNJRG0UlRqHaXYg0d3kWGTFa3tLTFtB51hweFYrZR76T3KLqQmoExEgfWptCLAwyyQBp' + \
           b'kUApgQ9ksrSAsX+92tJ/vuOLTMb3FwTA1hp1pN6itj95KualQwlYZIlIvaVIg1AKciOGw2CCeHomerchLvVejMP7+gymoGGR7ttXvVBT8vzLxlhTkl+O2SPK1332uOfz' + \
           b'+pAq/zgwRFt56LIyAGRtc+OR37b+uzRPfb1XEwCzIHwZH5Nar93pLSPmfnmlghsdHkOD4LRFjA1jnNPP3vpwP309hGzwlNcLQyj58XphSDtmTasT2y2sIi0BpC9L4hWI' + \
           b'twAYn68MQfr7eUMFMbwrk6MrvuwqjtcLKHBsY1J//BoLR/dnE/k6D/cnE/mc0Od83D+b0OfR/dlEvs7D/Xwi303ocz7uzyf0+zzcn03k6zzcjzeyx7+uuP9vLeK1Tf9P' + \
           b'JvQfaZEp644NXCjW1tzRe3Ifr2xGre9rCH7cAuDCAD98ddN98w4nYpgC+19UQVjzl9u7BwAAAAZiS0dEAP8A/wD/oL2nkwAAAAd0SU1FB+MGEQkHB+UVPj0AAAWTSURB' + \
           b'VGhD1Zh9TFVlHMe/59yLgPLS5XW4XqymTV5EU9NSg96Qywi3RGyz2VYbm5XSVZcUQcwREzYCTWwrcEIIoq4/1EFaBhIQsLzg5WU1K6EXKrtCEArChdvzPPccOAoX773n' + \
           b'XBif7e55uy/f3+98z+957uH0er0Z1uB5qP19YYjejKFWA3hunrBgO7x5DC6kPRL8GHpVaqgs04rBC+1kiHiVjwb6p6Iw2Pq9w+LV4PBRWAiMahfFxVOmDoBm3s8HLRui' + \
           b'MdL1A1Sch7BgO5bME/HLgjHAcZhntn6h5TA5AEG8/snnMdz1IxHvKSzYDss8EZ1DxN8kQThLPOXOAETbrNuIkd9+cjjz1Da5oc4XT5kIYA7ZRoolgDlmGyn8XLSNFJ7W' + \
           b'+blmGym8Qbt5ztlGClcAN7Mjm5RIbuhSoTc78I7usJTZFk/hjnJedl130TZZ5IaltDRVw3z7NobIUcFdxWPs5k2ofX1RVVWNpKRkVJ49iQB3N6g1GoSv3MA+Q7ly+VuY' + \
           b'entxfWgI2tgEYdZ+pj5KWEFabUT+O38Bf2seRJ9nEP6aH4jr/o+gm/dGyJkzqKg4DahVML4Ujz9d/dBEgqU0NlaxMZ2HSt4JyeYARPFitZkKv1/bETR8A+4FeTAdLIT/' + \
           b'6Ci0pEgE1daw9cGCAqSlvYPBY0VsTOfpuhxsCkC0DRVvrdosHOvDqph4hD++Hn3PPsPmbl38hrWJiW/A50odBnelIG7Nagy9uQ8aQx2bl8s9A5DaZrpSWV//ndADLl6s' + \
           b'Yu1YXx9rGxr0uEaspEqIgfGhUNZ2EuvQeblMGwAVb+smZTQahR7Q399v6YxZqhVlYGAAnK8f63P+/mysBFYDEG2jxCa1ZMnDWObnD9MnxdA018CUX8TGdF4uUwZAxas4' + \
           b'XpGzzfLlISjJ+AA3Hg1HgPEantuhQ8CNTjYuyUhn63KYFIBom9ywpYqcbYqLC9ETvAau2WmorKsn1rmFyto6uB14n8w/wdYdZZS87gjAEdt4b4pjFehu6Jzvnt3jfY+d' + \
           b'O5GSksHGtPV4O2nKz9nKMNGpIWV6fCdm4lnmlTlVsk2MEENKq9JQ8R5E3+7WDssVEG2j5JE4MDAQX1VbNjBKS0s9snIsV0AOVPwCmKFr68AIaXlHbOMoPb3/Cj3HEMXv' + \
           b'MXTARHSOkULDsxvWyf+kDIYGkinL7Ub74ssexm1DxNPMU/EUbn3wKvM/arXD4lVkR9Xra1Hf2ITGy83w0dyHF6Oj4OXpiZLyU8jNPoRLNV/iSOEx4RMWdrz2KiIjtMJo' + \
           b'esTMU8+LmRfhe4gAOZk/e+4U8j87irWrV2GE7K6aBfPh7eWF7EP54ISsRzwdjXd1u7DogftRXnSCvewVL7WNFF7OYTY9PRlFJ07ircTXsWLFOhw/fhqpqZnYuvUVxG2M' + \
           b'Et7lONZsI2XyjB0sXBjE2oqKStaKXL3aidAQef/WxMzTamOyIp4iKwARjvyYktzLNlJkBdDd3c1arTaatSKLFy+Coa1dGE1gJmJcXenDduuIttFNYxspsgJIT8/C9pe3' + \
           b'4PCnhWhursO2bfHYv/89lJeX4NyFr4V3WRgeHkbX73+g+tJ5VkIzM1OFlQmkthm1QTxFVgCUbiJq7eqVOJD3MUykHLf//AuyDh5mVYdmXKS0tAzbt25B4eel+DAnD5GR' + \
           b'EcKKBXtsI4ULC1sje/eKjX0Be/fq4ElqP6W1tQ1hYaGsv1LyJIJC942EhE2sX1b2BWsp9OGOjtZ5GzMvokgASrCPVhs7Mi8i20JyoLZxIQUseYod1lZmLQCx2ky3SdnC' + \
           b'rAQgrTb2ev5uZjwAR6uNNWY0AKVsI2XGAlDSNlJmJAClbSPF6QE4wzZSnBqAs2wjxWkBONM2Upzyrc62jRTFv5k+7vMzjZA/4O1Os80EwP/vHLXv3BH8dQAAAABJRU5E' + \
           b'rkJggg=='

# Taskbar Type Library (TLB) object.
TASKBAR_TLB_NAME = 'taskbar.tlb'

TASKBAR_TLB = b'TVNGVAIAAQAAAAAACQQAAAAAAABBAAAAAQAAAAAAAAAOAAAA/////wAAAAAAAAAATgAAADMDAAAAAAAA/////xgAAAAgAAAAgAAAAP////8AAAAAAAAAAGQAAADIAAAA' + \
              b'LAEAAJABAAD0AQAAWAIAALwCAAAgAwAAhAMAAOgDAABMBAAAsAQAABQFAAB8AQAAeAUAAP////8PAAAA/////wAAAAD/////DwAAAP////8AAAAA/////w8AAABMCAAA' + \
              b'EAAAAP////8PAAAA9AYAAIAAAAD/////DwAAAHQHAADYAAAA/////w8AAABcCAAAAAIAAP////8PAAAAXAoAAEQHAAD/////DwAAAP////8AAAAA/////w8AAACgEQAA' + \
              b'iAAAAP////8PAAAAKBIAACAAAAD/////DwAAAEgSAABUAAAA/////w8AAACcEgAAJAAAAP////8PAAAA/////wAAAAD/////DwAAAP////8AAAAA/////w8AAAAjIgAA' + \
              b'wBIAAAAAAAAAAAAAAwAAAAAAAAAFAAAAAAAAAAAAAAAAAAAAAAAAAGAAAAAAAAAAGAAAAAAAAAD/////AAAAAAAAAAD/////AQAgAAQAAABkAAAAAQADAAAAAAD/////' + \
              b'IyIBAKgTAAAAAAAAAAAAAAMAAAAAAAAAAwAAAAAAAAAAAAAAAAAAAAAAAAB4AAAAAAAAADAAAAAAAAAA/////wAAAAAAAAAA/////wAADAAEAAAA/////wAAAAAAAAAA' + \
              b'/////yYhAgAwFAAAAAAAAAAAAAADAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA/////wAAAABEAAAAAAAAAP////8AAAAAAAAAAP////8AAAAAEAAAAAgAAAAAAAAA' + \
              b'AAAAAP////8hIQMAMBQAAAAAAAAAAAAAAwAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAAAP////8AAAAAVAAAAAAAAAD/////AAAAAAAAAAD/////AAAAABAAAAD/////' + \
              b'AAAAAAAAAAD/////IyIEALQUAAAAAAAAAAAAAAMAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAAAAACQAAAAAAAAAMwBAAAAAAAA/////wAAAAAAAAAA/////wEAJAAEAAAA' + \
              b'AAAAAAIACAAAAAAA/////yEhBQD0FAAAAAAAAAAAAAADAAAAAAAAAAAABgAAAAAAAAAAAAAAAAAAAAAA/////wAAAAAgAgAAAAAAAP////8AAAAAAAAAAP////8AAAAA' + \
              b'HAIAAP////8AAAAAAAAAAP////8jIgYAuBUAAAAAAAAAAAAAAwAAAAAAAAAMAAAAAAAAAAAAAAAAAAAAAAAAAKgAAAAAAAAAsAIAAAAAAAD/////AAAAAAAAAAD/////' + \
              b'AQBUAAQAAACQAQAAAwAJAAAAAAD/////ICEHALwYAAAAAAAAAAAAAAMAAAAAAAAAAAAFAAAAAAAAAAAAAAAAAAAAAAD/////AAAAABQDAAAAAAAA/////wAAAAAAAAAA' + \
              b'/////wAAAAAEAAAA/////wAAAAAAAAAA/////yYhCABgGQAAAAAAAAAAAAADAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA/////wAAAADcAwAAAAAAAP////8AAAAA' + \
              b'AAAAAP////8AAAAABAAAAFAAAAAIAAAAAAAAAP////8hIQkAYBkAAAAAAAAAAAAAAwAAAAAAAAAAAAIAAAAAAAAAAAAAAAAAAAAAAP////8AAAAA8AMAAAAAAAD/////' + \
              b'AAAAAAAAAAD/////AAAAAAgAAAD/////AAAAAAAAAAD/////JyEKAKQZAAAAAAAAAAAAAAMAAAAAAAAAAAACAAAAAAAAAAAAAAAAAAAAAAD/////AAAAACAEAAAAAAAA' + \
              b'/////wAAAAAAAAAA/////wAAAAAEAAAA/////wAAAAAAAAAA/////yAhCwDoGQAAAAAAAAAAAAADAAAAAAAAAAAAAgAAAAAAAAAAAAAAAAAAAAAA/////wAAAAAMBQAA' + \
              b'AAAAAP////8AAAAAAAAAAP////8AAAAABAAAAP////8AAAAAAAAAAP////8hIQwALBoAAAAAAAAAAAAAAwAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAAAP////8AAAAA' + \
              b'oAYAAAAAAAD/////AAAAAAAAAAD/////AAAAABAAAAD/////AAAAAAAAAAD/////JSINALAaAAAAAAAAAAAAAAMAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADAAAAA' + \
              b'AgAAACwHAAAAAAAA/////wAAAAAAAAAA/////wEAAAAEAAAAAAAAAAAAAAAAAAAA/////3gAAACQAAAA/////////////////////8AAAAD/////////////////////' + \
              b'////////////////qAAAAP////////////////////8AAAAA/////////////////////0gAAAAYAAAA//////////////////////////8wAAAAQvY7aMrpJEG+Q2cG' + \
              b'Wy+mU/7/////////Zbp33nxR0RGi2gAA+Hc86f//////////Y7p33nxR0RGi2gAA+Hc86f//////////ZLp33nxR0RGi2gAA+Hc86f//////////QvP9Vm390BGVigBg' + \
              b'l8mgkAAAAAD/////AAAAAAAAAADAAAAAAAAARmQAAABgAAAAlUktYDqxm0Kmbhk15E9DF5ABAAD/////kfsa6iiehkuQ6Z6fil7vr1gCAAD/////RPP9Vm390BGVigBg' + \
              b'l8mgkBQFAAD/////WAIAAAEAAAD/////////////////////6AIAAP/////////////////////////////////////////////////////UBgAA4AUAAP//////////' + \
              b'//////////8sBwAAIAQAAP/////YBAAARAQAAP/////oAQAAoAYAAPgAAAC8BAAA/////3wEAAAMBAAA////////////////qAQAAP///////////////1AGAACsAwAA' + \
              b'KAMAAP/////wBAAA3AMAAP//////////nAIAAP///////////////5gFAAC4BQAAxAYAAP///////////////0QFAAD///////////////8YAAAA3AAAAP////+MAAAA' + \
              b'SAEAAAADAAAYBwAA/////8wBAACwAgAADAUAAOgGAAD//////////4wGAAD/////CAIAAP////9cAQAA//////////+YAQAAAAAAALABAAD/////////////////////' + \
              b'/////1gEAAD8BgAA////////////////////////////////////////////////tAYAAP////////////////////+ABQAA/////2wEAACUAwAA/////zQBAAAgAgAA' + \
              b'//////////////////////////8IAQAA/////1ACAAA8AgAAaAUAAIgCAAD/////FAMAAMgDAABEAAAA//////////8KANOVVGFza2JhckxpYldXAAAAAP////8MOD29' + \
              b'SVRhc2tiYXJMaXN0ZAAAAP////8IOFuISVVua25vd27IAAAA/////wQ4f/VHVUlELAEAAP////8rOALfX19NSURMX19fTUlETF9pdGZfVGFza2JhckxpYl8wMDA3XzAw' + \
              b'MDFfMDAwMVcsAQAA/////wUQQDFEYXRhMVdXVywBAAD/////BRBBMURhdGEyV1dXLAEAAP////8FEEIxRGF0YTNXV1csAQAA/////wUQQzFEYXRhNFdXV2QAAAD/////' + \
              b'DgC+jlF1ZXJ5SW50ZXJmYWNlV1f//////////wQAmzNyaWlk//////////8JAPb2cHB2T2JqZWN0V1dXZAAAAP////8GALW4QWRkUmVmV1dkAAAA/////wcAb2FSZWxl' + \
              b'YXNlVwAAAACgAAAABgDBGUhySW5pdFdXAAAAAP////8GAM/CQWRkVGFiV1f//////////wQAL8Fod25kAAAAAP////8JAEcJRGVsZXRlVGFiV1dXAAAAAP////8LANKD' + \
              b'QWN0aXZhdGVUYWJXAAAAAP////8OANQ5U2V0QWN0aXZhdGVBbHRXV5ABAAD/////DThF70lUYXNrYmFyTGlzdDJXV1eQAQAA/////xQAmQVNYXJrRnVsbHNjcmVlbldp' + \
              b'bmRvd///////////CwBN92ZGdWxsc2NyZWVuV/QBAAD/////Djhw9nRhZ1RIVU1CQlVUVE9OV1f0AQAA/////wYQedlkd01hc2tXV/QBAAD/////AxB4nmlJZFf0AQAA' + \
              b'/////wcQqxNpQml0bWFwV/QBAAD/////BRCeTWhJY29uV1dX9AEAAP////8FEPsMc3pUaXBXV1f0AQAAcAEAAAcQL4Bkd0ZsYWdzV1gCAAD/////DThG70lUYXNrYmFy' + \
              b'TGlzdDNXV1dYAgAA/////xAAk5hTZXRQcm9ncmVzc1ZhbHVl/////1QAAAAMAAJpdWxsQ29tcGxldGVk/////7QAAAAIAMIbdWxsVG90YWy8AgAA/////wc4feJUQlBG' + \
              b'TEFHV7wCAAD/////DzCpRlRCUEZfTk9QUk9HUkVTU1e8AgAA/////xIwqDxUQlBGX0lOREVURVJNSU5BVEVXV7wCAADIAAAACzDDqVRCUEZfTk9STUFMV7wCAAD/////' + \
              b'CjCnMVRCUEZfRVJST1JXV7wCAAD/////CzDtTVRCUEZfUEFVU0VEV1gCAABEAwAAEACoU1NldFByb2dyZXNzU3RhdGX//////////wgAfld0YnBGbGFncyADAAD/////' + \
              b'CDgsBXdpcmVIV05EhAMAAGQDAAAQOMPUX1JlbW90YWJsZUhhbmRsZYQDAAD/////CBAfY2ZDb250ZXh06AMAAP////8VOJRaX19NSURMX0lXaW5UeXBlc18wMDA5V1dX' + \
              b'6AMAAP////8HEJdKaElucHJvY1foAwAA/////wcQWpVoUmVtb3RlV4QDAAD/////ARBsEHVXV1dYAgAAdAIAAAsAHgFSZWdpc3RlclRhYlf//////////wcAHDZod25k' + \
              b'VGFiV///////////BwAjEWh3bmRNRElXWAIAAJQEAAANABymVW5yZWdpc3RlclRhYldXV1gCAAD/////CwAWi1NldFRhYk9yZGVyV/////9gAgAAEACrAmh3bmRJbnNl' + \
              b'cnRCZWZvcmVMBAAAgAEAAAg4x5VUQkFURkxBR0wEAAD/////FTDLIlRCQVRGX1VTRU1ESVRIVU1CTkFJTFdXV0wEAAD/////FzC5uFRCQVRGX1VTRU1ESUxJVkVQUkVW' + \
              b'SUVXV1gCAAD/////DAB661NldFRhYkFjdGl2Zf//////////CQBqkXRiYXRGbGFnc1dXV1gCAAD/////EgAzhVRodW1iQmFyQWRkQnV0dG9uc1dX//////////8IALRQ' + \
              b'Y0J1dHRvbnP/////IAEAAAcAtchwQnV0dG9uV1gCAAD/////FQAObVRodW1iQmFyVXBkYXRlQnV0dG9uc1dXV1gCAAAgBQAAFADLRVRodW1iQmFyU2V0SW1hZ2VMaXN0' + \
              b'//////////8EAI17aGltbFgCAAD/////DgBloFNldE92ZXJsYXlJY29uV1f/////fAMAAA4AJ6twc3pEZXNjcmlwdGlvbldXWAIAAP////8TAJr4U2V0VGh1bWJuYWls' + \
              b'VG9vbHRpcFf/////BAYAAAYAy6Fwc3pUaXBXV7AEAABsBgAABziayXRhZ1JFQ1RXsAQAADQGAAAEEOV7bGVmdLAEAADMBQAAAxA12nRvcFewBAAAJAYAAAUQDRVyaWdo' + \
              b'dFdXV7AEAAD/////BhBIe2JvdHRvbVdXWAIAADAAAAAQANtfU2V0VGh1bWJuYWlsQ2xpcP/////wAwAABwDDlXByY0NsaXBXFAUAAMwCAAALOBMKVGFza2Jhckxpc3RX' + \
              b'HAD+fwAAAAAdAP9/LAEAAB0A/3/IAAAAGgD/fxAAAAAaAABAGAAAgBoA/n8gAAAAHAD+fxAAAAAdAAMAvAIAAB0A/3/oAwAAHQD/f4QDAAAaAP9/SAAAAB0A/38gAwAA' + \
              b'HQADAEwEAAAdAP9/9AEAABoA/39oAAAAHQD/f7AEAAAaAP9/eAAAABEAEYABAAgACAAAAAAAAAASABKAAQAIAAQBAAAAAAAACAA+AAAAQ3JlYXRlZCBieSBNSURMIHZl' + \
              b'cnNpb24gOC4wMS4wNjIyIGF0IE1vbiBKYW4gMTggMTk6MTQ6MDcgMjAzOAoTAP///39XVxMAbgIBCFdXGAAAAAAAAAD/////MAAAAEQAAAAAAAAASAAAAEwAAAAMAAAA' + \
              b'qAAAABgAAAAZABmAAAAAAAwANAAJBAAAAAAAACQAAQAZABmAAAAAABAARAAJBAEAAQAAAAMAA4BwAQAAAQAAACQAAgAZABmAAAAAABQARAAJBAIAAQAAAAMAA4BwAQAA' + \
              b'AQAAACQAAwAZABmAAAAAABgARAAJBAMAAQAAAAMAA4BwAQAAAQAAACQABAAZABmAAAAAABwARAAJBAQAAQAAAAMAA4BwAQAAAQAAAAAAAWABAAFgAgABYAMAAWAEAAFg' + \
              b'SAEAAFwBAACAAQAAmAEAALABAAAAAAAAGAAAADwAAABgAAAAhAAAAGAAAAAwAAAAGQAZgAAAAAAAAGwACQQAAAIAAAAYAAAA+AAAAAEAAAAoAAAACAEAAAIAAAAYAAEA' + \
              b'EwATgAAAAAAEADQACQQBAAAAAAAYAAIAEwATgAAAAAAIADQACQQCAAAAAAAAAABgAQAAYAIAAGDcAAAAIAEAADQBAAAAAAAAMAAAAEgAAABQAAAAFAAAABMAE4AAAAAA' + \
              b'AAAkAAAAAAAUAAEAEgASgAAAAAAAACQABAAAABQAAgASABKAAAAAAAAAJAAGAAAAFAADAAAAAAAAAAAAAAA4AAgAAAAAAABAAQAAQAIAAEADAABAjAAAAKAAAAC0AAAA' + \
              b'yAAAAAAAAAAUAAAAKAAAADwAAAAwAAAAMAAAABkAGYAAAAAAIABUAAkEAAACAAAAAwADgHABAAABAAAAAwADgAgCAAABAAAAAAACYOgBAAAAAAAAeAAAABQAAAATABOA' + \
              b'AAAAAAAAJAAAAAAAFAABABcAE4AAAAAAAAAkAAQAAAAUAAIAFwATgAAAAAAAACQACAAAABQAAwANAA2AAAAAAAAAJAAMAAAAFAAEADAAAAAAAAAAAAA4ABAAAAAUAAUA' + \
              b'EwATgAAAAAAAACQAGAIAAAAAAEABAABAAgAAQAMAAEAEAABABQAAQDwCAABQAgAAYAIAAHQCAACIAgAAnAIAAAAAAAAUAAAAKAAAADwAAABQAAAAZAAAAHACAAA8AAAA' + \
              b'GQAZgAAAAAAkAGQACQQAAAMAAAADAAOAcAEAAAEAAAAVABWA6AIAAAEAAAAVABWAAAMAAAEAAAAwAAEAGQAZgAAAAAAoAFQACQQBAAIAAAADAAOAcAEAAAEAAAA4AAAA' + \
              b'yAMAAAEAAAAwAAIAGQAZgAAAAAAsAFQACQQCAAIAAAADAAOAlAQAAAEAAABYAAAAqAQAAAEAAAAkAAMAGQAZgAAAAAAwAEQACQQDAAEAAAADAAOAlAQAAAEAAAAwAAQA' + \
              b'GQAZgAAAAAA0AFQACQQEAAIAAAADAAOAlAQAAAEAAAADAAOA8AQAAAEAAAA8AAUAGQAZgAAAAAA4AGQACQQFAAMAAAADAAOAlAQAAAEAAAADAAOAqAQAAAEAAABgAAAA' + \
              b'gAUAAAEAAAA8AAYAGQAZgAAAAAA8AGwACQQGAAMAAAADAAOAcAEAAAEAAAAXABOAuAUAAAEAAABwAAAAzAUAAAEAAAA8AAcAGQAZgAAAAABAAGwACQQHAAMAAAADAAOA' + \
              b'cAEAAAEAAAAXABOAuAUAAAEAAABwAAAAzAUAAAEAAAAwAAgAGQAZgAAAAABEAFQACQQIAAIAAAADAAOAcAEAAAEAAAANAA2AJAYAAAEAAAA8AAkAGQAZgAAAAABIAGQA' + \
              b'CQQJAAMAAAADAAOAcAEAAAEAAAANAA2AdAIAAAEAAAAfAP7/UAYAAAEAAAAwAAoAGQAZgAAAAABMAFQACQQKAAIAAAADAAOAcAEAAAEAAAAfAP7/jAYAAAEAAAAwAAsA' + \
              b'GQAZgAAAAABQAFwACQQLAAIAAAADAAOAcAEAAAEAAACAAAAAGAcAAAEAAAAAAANgAQADYAIAA2ADAANgBAADYAUAA2AGAANgBwADYAgAA2AJAANgCgADYAsAA2DMAgAA' + \
              b'rAMAAHwEAAC8BAAA2AQAAGgFAACYBQAA4AUAAAQGAAA0BgAAbAYAAPwGAAAAAAAAPAAAAGwAAACcAAAAwAAAAPAAAAAsAQAAaAEAAKQBAADUAQAAEAIAAEACAABkAAAA' + \
              b'FAAAABYAA4AAAAAAAgA0AAAAAIwUAAEAFgADgAAAAAACADQAAQAAjBQAAgAWAAOAAAAAAAIANAACAACMFAADABYAA4AAAAAAAgA0AAQAAIwUAAQAFgADgAAAAAACADQA' + \
              b'CAAAjAAAAEABAABAAgAAQAMAAEAEAABAKAMAAEQDAABkAwAAfAMAAJQDAAAAAAAAFAAAACgAAAA8AAAAUAAAACgAAAAUAAAAAwADgAAAAAAAACQAAAAAABQAAQBAAAAA' + \
              b'AAAAAAAAJAAEAAAAAAAAQAEAAEAMBAAAbAQAAAAAAAAUAAAAKAAAABQAAAADAAOAAAAAAAAAJAAAAAAAFAABAAMAA4AAAAAAAAAkAAAAAAAAAABAAQAAQEQEAABYBAAA' + \
              b'AAAAABQAAAAoAAAAFAAAABYAA4AAAAAAAgA0AAEAAIwUAAEAFgADgAAAAAACADQAAgAAjAAAAEABAABAIAUAAEQFAAAAAAAAFAAAAFAAAAAUAAAAAwADgAAAAAAAACQA' + \
              b'AAAAABQAAQADAAOAAAAAAAAAJAAEAAAAFAACAAMAA4AAAAAAAAAkAAgAAAAUAAMAAwADgAAAAAAAACQADAAAAAAAAEABAABAAgAAQAMAAEC0BgAAxAYAANQGAADoBgAA' + \
              b'AAAAABQAAAAoAAAAPAAAAA=='

# Setup logger object.
g_Logger = logging.getLogger()

# Setup thread event.
g_stopEvent = threading.Event()

# Setup Windows taskbar object (if needed).
g_tlb = g_taskbar = None
if os.name == 'nt':
    g_tlb = open(TASKBAR_TLB_NAME, 'wb')
    g_tlb.write(base64.b64decode(TASKBAR_TLB))
    g_tlb.close()
    g_tlb = None
    
    try:
        import comtypes.client as cc
        
        g_tlb = cc.GetModule(TASKBAR_TLB_NAME)
        
        g_taskbar = cc.CreateObject('{56FDF344-FD6D-11D0-958A-006097C9A090}', interface=g_tlb.ITaskbarList3)
        g_taskbar.HrInit()
    except:
        traceback.print_exc()
    
    os.remove(TASKBAR_TLB_NAME)

# Reference: https://beenje.github.io/blog/posts/logging-to-a-tkinter-scrolledtext-widget.
class LogQueueHandler(logging.Handler):
    def __init__(self, log_queue):
        super().__init__()
        self.log_queue = log_queue
    
    def emit(self, record):
        self.log_queue.put(record)

# Reference: https://beenje.github.io/blog/posts/logging-to-a-tkinter-scrolledtext-widget.
class LogConsole:
    def __init__(self, scrolled_text):
        self.scrolled_text = scrolled_text
        self.frame = self.scrolled_text.winfo_toplevel()
        
        # Create a logging handler using a queue.
        self.log_queue = queue.Queue()
        self.queue_handler = LogQueueHandler(self.log_queue)
        #formatter = logging.Formatter('[%(asctime)s] -> %(message)s')
        formatter = logging.Formatter('%(message)s')
        self.queue_handler.setFormatter(formatter)
        g_Logger.addHandler(self.queue_handler)
        
        # Start polling messages from the queue.
        self.frame.after(100, self.poll_log_queue)
    
    def display(self, record):
        msg = self.queue_handler.format(record)
        self.scrolled_text.configure(state='normal')
        self.scrolled_text.insert(tk.END, msg + '\n', record.levelname)
        self.scrolled_text.configure(state='disabled')
        self.scrolled_text.yview(tk.END)
    
    def poll_log_queue(self):
        # Check every 100 ms if there is a new message in the queue to display.
        while True:
            try:
                record = self.log_queue.get(block=False)
            except queue.Empty:
                break
            else:
                self.display(record)
        
        self.frame.after(100, self.poll_log_queue)

# Loosely based on tk.py from tqdm.
class ProgressBarWindow:
    global g_tlb, g_taskbar
    
    def __init__(self, bar_format=None, tk_parent=None, window_title='', window_resize=False, window_protocol=None):
        if tk_parent is None: raise Exception('`tk_parent` must be provided!')
        
        self.n = 0
        self.total = 0
        self.divider = 1
        self.total_div = 0
        self.prefix = ''
        self.unit = 'B'
        self.bar_format = bar_format
        self.start_time = 0
        self.elapsed_time = 0
        self.hwnd = 0
        
        self.tk_parent = tk_parent
        
        self.tk_window = tk.Toplevel(self.tk_parent)
        
        self.tk_window.withdraw()
        self.withdrawn = True
        
        if window_title: self.tk_window.title(window_title)
        
        self.tk_window.resizable(window_resize, window_resize)
        
        if window_protocol: self.tk_window.protocol('WM_DELETE_WINDOW', window_protocol)
        
        pbar_frame = ttk.Frame(self.tk_window, padding=5)
        pbar_frame.pack()
        
        self.tk_text_var = tk.StringVar(self.tk_window)
        tk_label = ttk.Label(pbar_frame, textvariable=self.tk_text_var, wraplength=600, anchor='center', justify='center')
        tk_label.pack()
        
        self.tk_n_var = tk.DoubleVar(self.tk_window, value=0)
        self.tk_pbar = ttk.Progressbar(pbar_frame, variable=self.tk_n_var, length=450)
        self.tk_pbar.configure(maximum=100, mode='indeterminate')
        self.tk_pbar.pack()
    
    def __del__(self):
        self.tk_parent.after(0, self.tk_window.destroy)
    
    def start(self, total, n=0, divider=1, prefix='', unit='B'):
        if (total <= 0) or (n < 0) or (divider <= 0): raise Exception('Invalid arguments!')
        
        self.n = n
        self.total = total
        self.divider = float(divider)
        self.total_div = (float(self.total) / self.divider)
        self.prefix = prefix
        self.unit = unit
        
        self.tk_pbar.configure(maximum=self.total_div, mode='determinate')
        
        self.start_time = time.time()
    
    def update(self, n):
        cur_n = (self.n + n)
        if cur_n > self.total: return
        
        cur_n_div = (float(cur_n) / self.divider)
        self.elapsed_time = (time.time() - self.start_time)
        
        msg = tqdm.format_meter(n=cur_n_div, total=self.total_div, elapsed=self.elapsed_time, prefix=self.prefix, unit=self.unit, bar_format=self.bar_format)
        
        self.tk_text_var.set(msg)
        self.tk_n_var.set(cur_n_div)
        
        self.n = cur_n
        
        if self.withdrawn:
            self.tk_window.deiconify()
            self.setup_taskbar()
            self.tk_window.attributes('-topmost', True)
            self.tk_window.after(0, lambda: self.tk_window.attributes('-topmost', False))
            self.withdrawn = False
        
        if g_taskbar: g_taskbar.SetProgressValue(self.hwnd, cur_n, self.total)
    
    def end(self):
        self.n = 0
        self.total = 0
        self.divider = 1
        self.total_div = 0
        self.prefix = ''
        self.unit = 'B'
        self.start_time = 0
        self.elapsed_time = 0
        
        if g_taskbar:
            g_taskbar.SetProgressState(self.hwnd, g_tlb.TBPF_NOPROGRESS)
            g_taskbar.UnregisterTab(self.hwnd)
        
        self.tk_window.withdraw()
        self.withdrawn = True
        
        self.tk_pbar.configure(maximum=100, mode='indeterminate')
    
    def set_prefix(self, prefix):
        self.prefix = prefix
    
    def setup_taskbar(self):
        if not g_taskbar: return
        
        self.hwnd = int(self.tk_window.wm_frame(), 16)
        
        g_taskbar.ActivateTab(self.hwnd)
        g_taskbar.SetProgressState(self.hwnd, g_tlb.TBPF_NORMAL)

def utilsIsValueAlignedToEndpointPacketSize(value):
    return bool((value & (g_usbEpMaxPacketSize - 1)) == 0)

def utilsResetNspInfo():
    global g_nspTransferMode, g_nspSize, g_nspHeaderSize, g_nspRemainingSize, g_nspFile, g_nspFilePath
    
    # Reset NSP transfer mode info.
    g_nspTransferMode = False
    g_nspSize = 0
    g_nspHeaderSize = 0
    g_nspRemainingSize = 0
    g_nspFile = None
    g_nspFilePath = None

def utilsGetSizeUnitAndDivisor(size):
    size_suffixes = [ 'B', 'KiB', 'MiB', 'GiB' ]
    size_suffixes_count = len(size_suffixes)
    
    float_size = float(size)
    ret = None
    
    for i in range(size_suffixes_count):
        if (float_size < pow(1024, i + 1)) or ((i + 1) >= size_suffixes_count):
            ret = (size_suffixes[i], pow(1024, i))
            break
    
    return ret

def usbGetDeviceEndpoints():
    global g_usbEpIn, g_usbEpOut, g_usbEpMaxPacketSize
    
    prev_dev = cur_dev = None
    usb_ep_in_lambda = lambda ep: usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN
    usb_ep_out_lambda = lambda ep: usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_OUT
    usb_version = None
    
    while True:
        # Check if the user decided to stop the server.
        if g_stopEvent.is_set():
            g_stopEvent.clear()
            return False
        
        # Find a connected USB device with a matching VID/PID pair.
        cur_dev = usb.core.find(idVendor=USB_DEV_VID, idProduct=USB_DEV_PID)
        if (cur_dev is None) or ((prev_dev is not None) and (cur_dev.bus == prev_dev.bus) and (cur_dev.address == prev_dev.address)): # Using == here would also compare the backend.
            time.sleep(0.1)
            continue
        
        # Update previous device.
        prev_dev = cur_dev
        
        # Check if the product and manufacturer strings match the ones used by nxdumptool.
        #if (cur_dev.manufacturer != USB_DEV_MANUFACTURER) or (cur_dev.product != USB_DEV_PRODUCT):
        if cur_dev.manufacturer != USB_DEV_MANUFACTURER:
            g_Logger.error('Invalid manufacturer/product strings! (bus %u, address %u).' % (cur_dev.bus, cur_dev.address))
            time.sleep(0.1)
            continue
        
        # Reset device.
        cur_dev.reset()
        
        # Set default device configuration, then get the active configuration descriptor.
        cur_dev.set_configuration()
        cfg = cur_dev.get_active_configuration()
        
        # Get default interface descriptor.
        intf = cfg[(0,0)]
        
        # Retrieve endpoints.
        g_usbEpIn = usb.util.find_descriptor(intf, custom_match=usb_ep_in_lambda)
        g_usbEpOut = usb.util.find_descriptor(intf, custom_match=usb_ep_out_lambda)
        
        if (g_usbEpIn is None) or (g_usbEpOut is None):
            g_Logger.error('Invalid endpoint addresses! (bus %u, address %u).' % (cur_dev.bus, cur_dev.address))
            time.sleep(0.1)
            continue
        
        # Save endpoint max packet size and USB version.
        g_usbEpMaxPacketSize = g_usbEpIn.wMaxPacketSize
        usb_version = cur_dev.bcdUSB
        
        break
    
    g_tkCanvas.itemconfigure(g_tkTipMessage, state='normal', text=SERVER_STOP_MSG)
    
    g_Logger.debug('Successfully retrieved USB endpoints! (bus %u, address %u).' % (cur_dev.bus, cur_dev.address))
    g_Logger.debug('Max packet size: 0x%X (USB %u.%u).\n' % (g_usbEpMaxPacketSize, usb_version >> 8, (usb_version & 0xFF) >> 4))
    
    return True

def usbRead(size, timeout=-1):
    rd = None
    
    try:
        # Convert read data to a bytes object for easier handling.
        rd = bytes(g_usbEpIn.read(size, timeout))
    except:
        traceback.print_exc()
        g_Logger.error('USB timeout triggered or console disconnected.')
    
    return rd

def usbWrite(data, timeout=-1):
    wr = 0
    
    try:
        wr = g_usbEpOut.write(data, timeout)
    except:
        traceback.print_exc()
        g_Logger.error('USB timeout triggered or console disconnected.')
    
    return wr

def usbSendStatus(code):
    status = struct.pack('<4sIH6p', USB_MAGIC_WORD, code, g_usbEpMaxPacketSize, b'')
    return usbWrite(status, USB_TRANSFER_TIMEOUT) == len(status)

def usbHandleStartSession(cmd_block):
    global g_nxdtVersionMajor, g_nxdtVersionMinor, g_nxdtVersionMicro, g_nxdtAbiVersion, g_nxdtGitCommit
    
    g_Logger.debug('Received StartSession (%02X) command.' % (USB_CMD_START_SESSION))
    
    # Parse command block.
    (g_nxdtVersionMajor, g_nxdtVersionMinor, g_nxdtVersionMicro, g_nxdtAbiVersion, g_nxdtGitCommit) = struct.unpack_from('<BBBB8s', cmd_block, 0)
    g_nxdtGitCommit = g_nxdtGitCommit.decode('utf-8').strip('\x00')
    
    # Print client info.
    g_Logger.info('Client info: nxdumptool v%u.%u.%u, ABI v%u (commit %s).\n' % (g_nxdtVersionMajor, g_nxdtVersionMinor, g_nxdtVersionMicro, g_nxdtAbiVersion, g_nxdtGitCommit))
    
    # Check if we support this ABI version.
    if g_nxdtAbiVersion != USB_ABI_VERSION:
        g_Logger.error('Unsupported ABI version!')
        return USB_STATUS_UNSUPPORTED_ABI_VERSION
    
    # Return status code
    return USB_STATUS_SUCCESS

def usbHandleSendFileProperties(cmd_block):
    global g_nspTransferMode, g_nspSize, g_nspHeaderSize, g_nspRemainingSize, g_nspFile, g_nspFilePath, g_outputDir, g_tkRoot, g_progressBarWindow
    
    g_Logger.debug('Received SendFileProperties (%02X) command.' % (USB_CMD_SEND_FILE_PROPERTIES))
    
    # Parse command block.
    (file_size, filename_length, nsp_header_size, raw_filename) = struct.unpack_from('<QII{}s'.format(USB_FILE_PROPERTIES_MAX_NAME_LENGTH), cmd_block, 0)
    filename = raw_filename.decode('utf-8').strip('\x00')
    
    # Print info.
    info_str = ('File size: 0x%X | Filename length: 0x%X' % (file_size, filename_length))
    if nsp_header_size > 0: info_str += (' | NSP header size: 0x%X' % (nsp_header_size))
    info_str += '.'
    
    g_Logger.info(info_str)
    g_Logger.info('Filename: "%s".' % (filename))
    
    # Perform integrity checks
    if (not g_nspTransferMode) and file_size and (nsp_header_size >= file_size):
        g_Logger.error('NSP header size must be smaller than the full NSP size!\n')
        return USB_STATUS_MALFORMED_CMD
    
    if g_nspTransferMode and nsp_header_size:
        g_Logger.error('Received non-zero NSP header size during NSP transfer mode!\n')
        return USB_STATUS_MALFORMED_CMD
    
    if (not filename_length) or (filename_length > USB_FILE_PROPERTIES_MAX_NAME_LENGTH):
        g_Logger.error('Invalid filename length!\n')
        return USB_STATUS_MALFORMED_CMD
    
    # Enable NSP transfer mode (if needed).
    if (not g_nspTransferMode) and file_size and nsp_header_size:
        g_nspTransferMode = True
        g_nspSize = file_size
        g_nspHeaderSize = nsp_header_size
        g_nspRemainingSize = (file_size - nsp_header_size)
        g_nspFile = None
        g_nspFilePath = None
        g_Logger.debug('NSP transfer mode enabled!\n')
    
    # Perform additional integrity checks and get a file object to work with.
    if (not g_nspTransferMode) or (g_nspFile is None):
        # Check if we're dealing with an absolute path.
        if filename[0] == '/':
            filename = filename[1:]
            
            # Replace all slashes with backslashes if we're running under Windows.
            if os.name == 'nt': filename = filename.replace('/', '\\')
        
        # Generate full, absolute path to the destination file.
        fullpath = os.path.abspath(g_outputDir + os.path.sep + filename)
        
        # Get parent directory path.
        dirpath = os.path.dirname(fullpath)
        
        # Create full directory tree.
        os.makedirs(dirpath, exist_ok=True)
        
        # Make sure the output filepath doesn't point to an existing directory.
        if os.path.exists(fullpath) and (not os.path.isfile(fullpath)):
            utilsResetNspInfo()
            g_Logger.error('Output filepath points to an existing directory! ("%s").\n' % (fullpath))
            return USB_STATUS_HOST_IO_ERROR
        
        # Make sure we have enough free space.
        (total_space, used_space, free_space) = shutil.disk_usage(dirpath)
        if free_space <= file_size:
            utilsResetNspInfo()
            g_Logger.error('Not enough free space available in output volume!\n')
            return USB_STATUS_HOST_IO_ERROR
        
        # Get file object.
        file = open(fullpath, "wb")
        
        if g_nspTransferMode:
            # Update NSP file object.
            g_nspFile = file
            
            # Update NSP file path.
            g_nspFilePath = fullpath
            
            # Write NSP header padding right away.
            file.write(b'\0' * g_nspHeaderSize)
    else:
        # Retrieve what we need using global variables.
        file = g_nspFile
        fullpath = g_nspFilePath
        dirpath = os.path.dirname(fullpath)
    
    # Check if we're dealing with an empty file or with the first SendFileProperties command from a NSP.
    if (not file_size) or (g_nspTransferMode and file_size == g_nspSize):
        # Close file (if needed).
        if not g_nspTransferMode: file.close()
        
        # Let the command handler take care of sending the status response for us.
        return USB_STATUS_SUCCESS
    
    # Send status response before entering the data transfer stage.
    usbSendStatus(USB_STATUS_SUCCESS)
    
    # Start data transfer stage.
    file_type_str = ('file' if (not g_nspTransferMode) else 'NSP file entry')
    g_Logger.info('Data transfer started. Saving %s to: "%s".' % (file_type_str, fullpath))
    
    offset = 0
    blksize = USB_TRANSFER_BLOCK_SIZE
    
    # Check if we should use the progress bar window.
    use_pbar = (((not g_nspTransferMode) and (file_size > USB_TRANSFER_THRESHOLD)) or (g_nspTransferMode and (g_nspSize > USB_TRANSFER_THRESHOLD)))
    if use_pbar:
        idx = filename.rfind(os.path.sep)
        prefix_filename = (filename[idx+1:] if (idx >= 0) else filename)
        
        prefix = ('Current %s: "%s".\n' % (file_type_str, prefix_filename))
        prefix += 'Use your console to cancel the file transfer if you wish to do so.\n\n'
        
        if (not g_nspTransferMode) or g_nspRemainingSize == (g_nspSize - g_nspHeaderSize):
            if not g_nspTransferMode:
                # Set current progress to zero and the maximum value to the provided file size.
                pbar_n = 0
                pbar_file_size = file_size
            else:
                # Set current progress to the NSP header size and the maximum value to the provided NSP size.
                pbar_n = g_nspHeaderSize
                pbar_file_size = g_nspSize
            
            # Get progress bar unit and unit divider. These will be used to display and calculate size values using a specific size unit (B, KiB, MiB, GiB).
            (unit, unit_divider) = utilsGetSizeUnitAndDivisor(pbar_file_size)
            
            # Initialize progress bar.
            g_progressBarWindow.start(pbar_file_size, pbar_n, unit_divider, prefix, unit)
        else:
            # Set current prefix (holds the filename for the current NSP file entry).
            g_progressBarWindow.set_prefix(prefix)
    
    def cancelTransfer():
        # Cancel file transfer.
        file.close()
        os.remove(fullpath)
        utilsResetNspInfo()
        if use_pbar: g_progressBarWindow.end()
    
    # Start transfer process.
    start_time = time.time()
    
    while offset < file_size:
        # Update block size (if needed).
        diff = (file_size - offset)
        if blksize > diff: blksize = diff
        
        # Set block size and handle Zero-Length Termination packet (if needed).
        rd_size = blksize
        if ((offset + blksize) >= file_size) and utilsIsValueAlignedToEndpointPacketSize(blksize): rd_size += 1
        
        # Read current chunk.
        chunk = usbRead(rd_size, USB_TRANSFER_TIMEOUT)
        if chunk is None:
            g_Logger.error('Failed to read 0x%X-byte long data chunk!' % (rd_size))
            
            # Cancel file transfer.
            cancelTransfer()
            
            # Returning None will make the command handler exit right away.
            return None
        
        chunk_size = len(chunk)
        
        # Check if we're dealing with a CancelFileTransfer command.
        if chunk_size == USB_CMD_HEADER_SIZE:
            (magic, cmd_id, cmd_block_size) = struct.unpack_from('<4sII', chunk, 0)
            if (magic == USB_MAGIC_WORD) and (cmd_id == USB_CMD_CANCEL_FILE_TRANSFER):
                g_Logger.debug('\nReceived CancelFileTransfer (%02X) command.\n' % (USB_CMD_CANCEL_FILE_TRANSFER))
                
                # Cancel file transfer.
                cancelTransfer()
                
                # Let the command handler take care of sending the status response for us.
                return USB_STATUS_SUCCESS
        
        # Write current chunk.
        file.write(chunk)
        file.flush()
        
        # Update current offset.
        offset = (offset + chunk_size)
        
        # Update remaining NSP data size.
        if g_nspTransferMode: g_nspRemainingSize -= chunk_size
        
        # Update progress bar window (if needed).
        if use_pbar: g_progressBarWindow.update(chunk_size)
    
    elapsed_time = round(time.time() - start_time)
    g_Logger.info('File transfer successfully completed in %s!\n' % (tqdm.format_interval(elapsed_time)))
    
    # Close file handle (if needed).
    if not g_nspTransferMode: file.close()
    
    # Hide progress bar window (if needed).
    if use_pbar and ((not g_nspTransferMode) or (not g_nspRemainingSize)): g_progressBarWindow.end()
    
    return USB_STATUS_SUCCESS

def usbHandleSendNspHeader(cmd_block):
    global g_nspTransferMode, g_nspHeaderSize, g_nspRemainingSize, g_nspFile, g_nspFilePath
    
    nsp_header_size = len(cmd_block)
    
    g_Logger.debug('Received SendNspHeader (%02X) command.' % (USB_CMD_SEND_NSP_HEADER))
    
    # Integrity checks.
    if not g_nspTransferMode:
        g_Logger.error('Received NSP header out of NSP transfer mode!\n')
        return USB_STATUS_MALFORMED_CMD
    
    if g_nspRemainingSize:
        g_Logger.error('Received NSP header before receiving all NSP data! (missing 0x%X byte[s]).\n' % (g_nspRemainingSize))
        return USB_STATUS_MALFORMED_CMD
    
    if nsp_header_size != g_nspHeaderSize:
        g_Logger.error('NSP header size mismatch! (0x%X != 0x%X).\n' % (nsp_header_size, g_nspHeaderSize))
        return USB_STATUS_MALFORMED_CMD
    
    # Write NSP header.
    g_nspFile.seek(0)
    g_nspFile.write(cmd_block)
    g_nspFile.close()
    
    g_Logger.debug('Successfully wrote 0x%X-byte long NSP header to "%s".\n' % (nsp_header_size, g_nspFilePath))
    
    # Disable NSP transfer mode.
    utilsResetNspInfo()
    
    return USB_STATUS_SUCCESS

def usbHandleEndSession(cmd_block):
    g_Logger.debug('Received EndSession (%02X) command.' % (USB_CMD_END_SESSION))
    return USB_STATUS_SUCCESS

def usbCommandHandler():
    # CancelFileTransfer is handled in usbHandleSendFileProperties().
    cmd_dict = {
        USB_CMD_START_SESSION:        usbHandleStartSession,
        USB_CMD_SEND_FILE_PROPERTIES: usbHandleSendFileProperties,
        USB_CMD_SEND_NSP_HEADER:      usbHandleSendNspHeader,
        USB_CMD_END_SESSION:          usbHandleEndSession
    }
    
    # Get device endpoints.
    if not usbGetDeviceEndpoints():
        # Update UI and return.
        uiToggleElements(True)
        return
    
    # Disable server button.
    g_tkServerButton.configure(state='disabled')
    
    # Reset NSP info.
    utilsResetNspInfo()
    
    while True:
        # Read command header.
        cmd_header = usbRead(USB_CMD_HEADER_SIZE)
        if (cmd_header is None) or (len(cmd_header) != USB_CMD_HEADER_SIZE):
            g_Logger.error('Failed to read 0x%X-byte long command header!' % (USB_CMD_HEADER_SIZE))
            break
        
        # Parse command header.
        (magic, cmd_id, cmd_block_size) = struct.unpack_from('<4sII', cmd_header, 0)
        
        # Read command block right away (if needed).
        # nxdumptool expects us to read it right after sending the command header.
        cmd_block = None
        if cmd_block_size:
            # Handle Zero-Length Termination packet (if needed).
            if utilsIsValueAlignedToEndpointPacketSize(cmd_block_size):
                rd_size = (cmd_block_size + 1)
            else:
                rd_size = cmd_block_size
            
            cmd_block = usbRead(rd_size, USB_TRANSFER_TIMEOUT)
            if (cmd_block is None) or (len(cmd_block) != cmd_block_size):
                g_Logger.error('Failed to read 0x%X-byte long command block for command ID %02X!' % (cmd_block_size, cmd_id))
                break
        
        # Verify magic word.
        if magic != USB_MAGIC_WORD:
            g_Logger.error('Received command header with invalid magic word!\n')
            usbSendStatus(USB_STATUS_INVALID_MAGIC_WORD)
            continue
        
        # Get command handler function.
        cmd_func = cmd_dict.get(cmd_id, None)
        if cmd_func is None:
            g_Logger.error('Received command header with unsupported ID %02X.\n' % (cmd_id))
            usbSendStatus(USB_STATUS_UNSUPPORTED_CMD)
            continue
        
        # Verify command block size.
        if (cmd_id == USB_CMD_START_SESSION and cmd_block_size != USB_CMD_BLOCK_SIZE_START_SESSION) or \
           (cmd_id == USB_CMD_SEND_FILE_PROPERTIES and cmd_block_size != USB_CMD_BLOCK_SIZE_SEND_FILE_PROPERTIES) or \
           (cmd_id == USB_CMD_SEND_NSP_HEADER and not cmd_block_size):
            g_Logger.error('Invalid command block size for command ID %02X! (0x%X).\n' % (cmd_id, cmd_block_size))
            usbSendStatus(USB_STATUS_MALFORMED_COMMAND)
            continue
        
        # Run command handler function.
        # Send status response afterwards. Bail out if requested.
        status = cmd_func(cmd_block)
        if (status is None) or (not usbSendStatus(status)) or (cmd_id == USB_CMD_END_SESSION) or (status == USB_STATUS_UNSUPPORTED_ABI_VERSION):
            break
    
    g_Logger.info('Stopping server.')
    
    # Update UI.
    uiToggleElements(True)

def uiStopServer():
    # Signal the shared stop event.
    g_stopEvent.set()

def uiStartServer():
    global g_outputDir
    
    g_outputDir = g_tkDirText.get('1.0', tk.END).strip()
    if not g_outputDir:
        # We should never reach this, honestly.
        messagebox.showerror('Error', 'You must provide an output directory!', parent=g_tkRoot)
        return
    
    # Make sure the full directory tree exists.
    try:
        os.makedirs(g_outputDir, exist_ok=True)
    except:
        traceback.print_exc()
        messagebox.showerror('Error', 'Unable to create full output directory tree!', parent=g_tkRoot)
        return
    
    # Update UI.
    uiToggleElements(False)
    
    # Create background server thread.
    server_thread = threading.Thread(target=usbCommandHandler, daemon=True)
    server_thread.start()

def uiToggleElements(enable):
    if enable:
        g_tkRoot.protocol('WM_DELETE_WINDOW', uiHandleExitProtocol)
        
        g_tkChooseDirButton.configure(state='normal')
        g_tkServerButton.configure(text='Start server', command=uiStartServer, state='normal')
        g_tkCanvas.itemconfigure(g_tkTipMessage, state='hidden', text='')
    else:
        g_tkRoot.protocol('WM_DELETE_WINDOW', uiHandleExitProtocolStub)
        
        g_tkChooseDirButton.configure(state='disabled')
        g_tkServerButton.configure(text='Stop server', command=uiStopServer, state='normal')
        g_tkCanvas.itemconfigure(g_tkTipMessage, state='normal', text=SERVER_START_MSG)
        
        g_tkScrolledTextLog.configure(state='normal')
        g_tkScrolledTextLog.delete('1.0', tk.END)
        g_tkScrolledTextLog.configure(state='disabled')

def uiChooseDirectory():
    dir = filedialog.askdirectory(parent=g_tkRoot, title='Select an output directory', initialdir=INITIAL_DIR, mustexist=True)
    if dir: uiUpdateDirectoryField(os.path.abspath(dir))

def uiUpdateDirectoryField(dir):
    g_tkDirText.configure(state='normal')
    g_tkDirText.delete('1.0', tk.END)
    g_tkDirText.insert('1.0', dir)
    g_tkDirText.configure(state='disabled')

def uiHandleExitProtocol():
    g_tkRoot.destroy()

def uiHandleExitProtocolStub():
    pass

def uiScaleMeasure(measure):
    return round(float(measure) * SCALE)

def main():
    global SCALE, g_tkRoot, g_tkCanvas, g_tkDirText, g_tkChooseDirButton, g_tkServerButton, g_tkTipMessage, g_tkScrolledTextLog, g_progressBarWindow
    
    # Disable warnings.
    warnings.filterwarnings("ignore")
    
    # Get OS information.
    os_type = platform.system()
    os_version = platform.version()
    
    # Check if we're running under Windows Vista or later.
    dpi_aware = False
    win_vista = ((os_type == 'Windows') and (int(os_version[:os_version.find('.')]) >= 6))
    if win_vista:
        try:
            # Enable high DPI scaling.
            dpi_aware = (ctypes.windll.user32.SetProcessDPIAware() == 1)
            if not dpi_aware: dpi_aware = (ctypes.windll.shcore.SetProcessDpiAwareness(1) == 0)
        except:
            traceback.print_exc()
    
    # Create root Tkinter object.
    g_tkRoot = tk.Tk()
    
    # Get screen resolution.
    screen_width_px = g_tkRoot.winfo_screenwidth()
    screen_height_px = g_tkRoot.winfo_screenheight()
    
    # Get pixel density (DPI).
    screen_dpi = round(g_tkRoot.winfo_fpixels('1i'))
    
    # Update scaling factor (if needed).
    if win_vista and dpi_aware: SCALE = (float(screen_dpi) / WINDOWS_SCALING_FACTOR)
    
    # Decode embedded icon and set it.
    try:
        icon_fp = io.BytesIO(base64.b64decode(APP_ICON))
        icon_image = Image.open(fp=icon_fp, formats=['PNG'])
        icon_photo = ImageTk.PhotoImage(icon_image)
        
        g_tkRoot.wm_iconphoto(True, icon_photo)
        
        icon_image.close()
        icon_fp.close()
    except:
        traceback.print_exc()
        g_tkRoot.withdraw()
        messagebox.showerror('Error', 'Unable to decode embedded application icon!', parent=g_tkRoot)
        g_tkRoot.destroy()
        return
    
    # Set window properties.
    g_tkRoot.resizable(False, False)
    g_tkRoot.title("{} host app v{}".format(USB_DEV_PRODUCT, APP_VERSION))
    g_tkRoot.protocol('WM_DELETE_WINDOW', uiHandleExitProtocol)
    
    # Determine window size.
    window_width_px = uiScaleMeasure(WINDOW_WIDTH)
    window_height_px = uiScaleMeasure(WINDOW_HEIGHT)
    
    # Center window.
    pos_hor = int((screen_width_px / 2) - (window_width_px / 2))
    pos_ver = int((screen_height_px / 2) - (window_height_px / 2))
    g_tkRoot.geometry("+{}+{}".format(pos_hor, pos_ver))
    
    # Create canvas and fill it with window elements.
    g_tkCanvas = tk.Canvas(g_tkRoot, width=window_width_px, height=window_height_px)
    g_tkCanvas.pack()
    
    g_tkCanvas.create_text(uiScaleMeasure(60), uiScaleMeasure(30), text='Output directory:', anchor=tk.CENTER)
    
    g_tkDirText = tk.Text(g_tkRoot, height=1, width=45, font=font.nametofont('TkDefaultFont'), wrap='none', state='disabled', bg='#F0F0F0')
    uiUpdateDirectoryField(DEFAULT_DIR)
    g_tkCanvas.create_window(uiScaleMeasure(260), uiScaleMeasure(30), window=g_tkDirText, anchor=tk.CENTER)
    
    g_tkChooseDirButton = tk.Button(g_tkRoot, text='Choose', width=10, command=uiChooseDirectory)
    g_tkCanvas.create_window(uiScaleMeasure(450), uiScaleMeasure(30), window=g_tkChooseDirButton, anchor=tk.CENTER)
    
    g_tkServerButton = tk.Button(g_tkRoot, text='Start server', width=15, command=uiStartServer)
    g_tkCanvas.create_window(uiScaleMeasure(WINDOW_WIDTH / 2), uiScaleMeasure(70), window=g_tkServerButton, anchor=tk.CENTER)
    
    g_tkTipMessage = g_tkCanvas.create_text(uiScaleMeasure(WINDOW_WIDTH / 2), uiScaleMeasure(100), anchor=tk.CENTER)
    g_tkCanvas.itemconfigure(g_tkTipMessage, state='hidden', text='')
    
    g_tkScrolledTextLog = scrolledtext.ScrolledText(g_tkRoot, height=20, width=65, font=font.nametofont('TkDefaultFont'), wrap=tk.WORD, state='disabled')
    g_tkScrolledTextLog.tag_config('DEBUG', foreground='gray')
    g_tkScrolledTextLog.tag_config('INFO', foreground='black')
    g_tkScrolledTextLog.tag_config('WARNING', foreground='orange')
    g_tkScrolledTextLog.tag_config('ERROR', foreground='red')
    g_tkScrolledTextLog.tag_config('CRITICAL', foreground='red', underline=1)
    g_tkCanvas.create_window(uiScaleMeasure(WINDOW_WIDTH / 2), uiScaleMeasure(280), window=g_tkScrolledTextLog, anchor=tk.CENTER)
    
    g_tkCanvas.create_text(uiScaleMeasure(5), uiScaleMeasure(WINDOW_HEIGHT - 10), text="Copyright (c) {}, {}".format(COPYRIGHT_YEAR, USB_DEV_MANUFACTURER), anchor=tk.W)
    
    # Configure logging mechanism.
    logging.basicConfig(level=logging.DEBUG)
    if len(g_Logger.handlers):
        # Remove stderr output handler from logger.
        log_stderr = g_Logger.handlers[0]
        g_Logger.removeHandler(log_stderr)
    
    # Initialize console logger.
    console = LogConsole(g_tkScrolledTextLog)
    
    # Create hidden progress bar window.
    bar_format = '{desc}{percentage:.2f}% - {n:.2f} / {total:.2f} {unit}\nElapsed time: {elapsed}. Remaining time: {remaining}.\nSpeed: {rate_fmt}.'
    g_progressBarWindow = ProgressBarWindow(bar_format, g_tkRoot, 'File transfer', False, uiHandleExitProtocolStub)
    
    # Enter Tkinter main loop.
    g_tkRoot.mainloop()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
