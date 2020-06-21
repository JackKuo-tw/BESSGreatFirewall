# BESSGreatFirewall

此為清華大學 108 學年度下學期碩士班課程「雲端計算」之期末專題。

英文題目：**NFV Great Firewall by Machine Learning test with Docker Website**

中文題目：**以 NFV 實作之防火長城並藉由機器學習技術辨識封包內容再以 Docker 進行測試**

題目發想來源：組員依課程要求閱讀雲端相關技術論文，其中提到 BESS 這個 NFV 常用之軟體交換器，覺得十分有趣因此花了幾週的時間研究這一百多萬行 C/C++ 之程式，搭配課程作業之機器學習內容與 Container 技術來實作簡單版防火長城。

## 實作內容

當使用者送出 HTTP Request 時，防火長城會偵測封包內容：

1. 若是含有敏感字詞，則會對 src 與 dst 兩端分別送出 RST 之封包以中斷連線
2. 若是請求圖片，則防火長城會偵測圖片，並將圖片交由 Tensorflow 程式進行辨識，接著把辨識結果記錄下來，以網頁方式呈現歷史紀錄

### 技術細節

**防火長城**：使用 C/C++ 進行撰寫主要偵測、封包處理程式碼，配合 Protocol Buffers 以 Python 進行操控。

**網頁**：使用 Flask 隨機挑選圖片顯示，該網頁包在 Docker 當中。

**辨識**：使用 Tensorflow 進行圖片辨識，因圖片中只有貓與狗，辨識結果只會顯示貓或狗。

使用 BESS 在在本機與 Docker 上建立網卡，使用 DPDK 技術打通兩邊的連線，

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

