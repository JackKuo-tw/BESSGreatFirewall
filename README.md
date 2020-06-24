# BESSGreatFirewall

此為清華大學 108 學年度下學期碩士班課程「雲端計算」之期末專題。

英文題目：**NFV Great Firewall by Machine Learning test with Docker Website**

中文題目：**以 NFV 實作之防火長城並藉由機器學習技術辨識封包內容再以 Docker 進行測試**

題目發想來源：組員依課程要求閱讀雲端相關技術論文，其中提到 [BESS](https://github.com/NetSys/bess/) 這個 NFV 常用之軟體交換器，覺得十分有趣因此花了幾週的時間研究這一百多萬行 C/C++ 之程式，搭配課程作業之機器學習內容與 Container 技術來實作簡單版防火長城。

## 實作內容

當使用者送出 HTTP Request 時，防火長城會偵測封包內容：

1. 若是含有敏感字詞，則會對 src 與 dst 兩端分別送出 RST 之封包以中斷連線
2. 若是請求圖片，則防火長城會偵測圖片，並將圖片交由 Tensorflow 程式進行辨識，接著把辨識結果記錄下來，以網頁方式呈現歷史紀錄

### 技術細節

**防火長城**：使用 C/C++ 進行撰寫主要偵測、封包處理程式碼，配合 Protocol Buffers 以 Python 進行操控。

**網頁**：使用 Flask 隨機挑選圖片顯示，該網頁包在 Docker 當中。

**辨識**：使用 Tensorflow 進行圖片辨識，因圖片中只有貓與狗，辨識結果只會顯示貓或狗。

使用 BESS 在在本機與 Docker 上建立網卡，使用 DPDK 技術打通兩邊的連線，防火長城會持續對封包內容進行過濾，並在本機端（或是遠端）啟動 Tensorflow 辨識之 daemon。

### 路由拓樸圖

- vport0：網站所在地
- vport1：瀏覽器所在地
- filter：防火長城

```
+--------------+            +---------------+            +--------------+            +--------------+
|  port_inc0   |            |    filter     |            |    merge     |            |  port_out1   |
|   PortInc    |  :0 0 0:   | GreatFirewall |  :1 0 0:   |    Merge     |  :0 0 0:   |   PortOut    |
| vport1/VPort | ---------> |    2 hosts    | ---------> |              | ---------> | vport1/VPort |
+--------------+            +---------------+            +--------------+            +--------------+
                              |                            ^
                              | :0 0 0:                    | :0 0 1:
                              v                            |
                            +---------------+            +--------------+
                            |   port_out0   |            |  port_inc1   |
                            |    PortOut    |            |   PortInc    |
                            | vport0/VPort  |            | vport0/VPort |
                            +---------------+            +--------------+
```

## 部署

### 啟動虛擬機

- 在 `/bess/env/Vagrantfile` 中可以調整 vb.cpus 數量，以加快編譯速度
- 在 `/bess/env/` 中使用指令 `vagrant up` 建立虛擬環境
- (選用) 在虛擬環境建立完成後，使用指令 `vagrant ssh` 連線進入虛擬機中
- 在 VM 視窗中使用帳密 `vagrant` : `vagrant` 登入後，使用指令 `startx` 啟動桌面環境

### 虛擬機內執行

- 首次進入虛擬機，需編譯必要套件
  - 在 `~/BESSGreatFirewall/bess` 中，使用指令 `./biuld.py`，編譯 DPDK 與 BESS 程式碼
  - 在 `~/BESSGreatFirewall/web` 中，使用指令 `./run.sh` 建立 Docker image 同時啟用
  - 在 `~/BESSGreatFirewall/web` 中，使用指令 `sudo pip3 install -r requirements.txt` 安裝本地端辨識程式所需套件
  - (選用) `sudo apt install firefox`，內建瀏覽器不好用...
- `~/BESSGreatFirewall/bess/bessctl` 中使用指令 `./bessctl` 啟用控制程式
  - 輸入 `daemon start` 可以啟用 BESS，`daemon stop` 則是停止
  - `run great_firewall` 啟用防火長城
  - `monitor pipe` 觀察網路拓樸
- 造訪網頁
  - 目標網頁：`http://10.255.99.2`（寫在 `great_firewall.bess` 中）
  - 紀錄網頁：`http://localhost:8000/statics`

- 觀察 log
  - `tail -f /tmp/bessd.INFO`
  - `tail -f /tmp/bessd.WARNING`
  - `tail -f /tmp/bessd.ERROR`
  - `tail -f /tmp/bessd.FATAL`

### 修改程式

- 修改 `.bess` 程式皆不需要編譯（[BESS 語法糖](https://github.com/NetSys/bess/blob/master/bessctl/sugar.py)）
- 修改 `.h`, `.cc` `.proto` 等程式皆需要重新編譯
  - `./build.py bess`（可避免 DPDK 重新編譯，縮短時間）
- 開發指引
  - [commit: c0ebdb](https://github.com/JackKuo-tw/BESSGreatFirewall/commit/c0ebdba0bbbc0a3989d00f2f3129e761803c47af)
  - [commit: a380a5](https://github.com/JackKuo-tw/BESSGreatFirewall/commit/a380a5efef43c7c75c418899a61f3c1c9ebcec4e)

## 故障排除

- 巢狀虛擬化必須打開，不然 `bessctl` 的 `deamon run` 會失敗
  - Mac 用戶可能會無法透過 GUI 打開，請用指令：`VBoxManage modifyvm "BESS Great Firewall" --nested-hw-virt on`

