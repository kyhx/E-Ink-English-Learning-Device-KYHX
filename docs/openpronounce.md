# OpenPronounce 研究、本地部署与本项目关联

> 上游：<https://github.com/Halleck45/OpenPronounce>  
> 本地克隆：`third_party/OpenPronounce`（commit `74bc17e`，v0.3.0，MIT；已 gitignore）  
> 本地服务：<http://127.0.0.1:8000>（uvicorn，当前在跑）  
> 日期：2026-10-26

---

## 第一部分：OpenPronounce 是什么

### 1.1 定位

OpenPronounce 是一个**自托管、音素级**的英语发音评测工具，目标是做 Azure Pronunciation Assessment / SpeechAce / ELSA 的开源替代：

- 输入：一段录音 + 这段录音“应该说的句子”；
- 输出：0–100 分、转写文本、读错的单词、每个单词“实际听到的音素（IPA）”、音素错误率、词错误率、音高/能量曲线、声学距离；
- 全部在本地 CPU 跑，不上传录音，不需要 API key。

它和“语音识别”不同：ASR 会利用语言模型把 learner 的错误“自动纠正”成正确单词；OpenPronounce 特意用**音素识别器直接听声音**，因此能指出“你把 /θ/ 读成了 /s/”这类错误。

### 1.2 处理链

```text
录音 (16 kHz mono)
  ├─ Wav2Vec2 phone model (espeak IPA)
  │     -> 实际听到的音素序列 + CTC 置信度
  ├─ 文本 -> espeak-ng -> 期望音素序列
  ├─ Levenshtein 对齐 -> 替换/删除/插入 + 每音素置信度
  ├─ Wav2Vec2-large-960h -> 转写文本 -> WER
  ├─ 参考语音 (gTTS/Piper/Kokoro) -> Wav2Vec2 embedding -> DTW -> acoustic_distance
  └─ pYIN + RMS -> F0 / energy
```

评分：

```text
score = 0.3 × acoustic_term
      + 0.4 × (1 − phoneme_error_rate)
      + 0.3 × (1 − word_error_rate)
```

三项都裁到 0–100；英语 acoustic_distance 6 → 100，15 → 0。权重是在 **speechocean762 500 条**上拟合/选择的，不是拍脑袋。

### 1.3 模型与依赖

| 项 | 内容 |
| --- | --- |
| 音素识别 | `facebook/wav2vec2-lv-60-espeak-cv-ft` |
| 英文转写/embedding | `facebook/wav2vec2-large-960h` |
| 其他语言 | 各语言 XLSR-53 模型（实验性） |
| 权重体积 | 两个 checkpoint 各约 **1.2 GB**，合计约 2.4 GB |
| 运行 | CPU 可跑；官方 benchmark 约 **3.0 s/句**（6 线程） |
| 依赖 | Python 3.10+、torch、transformers、librosa、phonemizer、espeak-ng、ffmpeg、gTTS |
| License | 代码 **MIT**；模型权重有各自 license，需单独确认 |

### 1.4 官方 benchmark（500 条 speechocean762）

| 指标 | 结果 |
| --- | --- |
| score vs human total | Pearson 0.633 / Spearman **0.652** |
| score vs human accuracy | 0.603 / 0.631 |
| acoustic_distance vs human total | −0.639 / −0.666 |
| 单词级“读错”检测 | recall **0.732**，precision **0.196**，F1 0.309 |
| 每说话人均分 vs 人均 human total | Spearman 0.825 |

单词级 precision 只有 0.196：它倾向“多报”——每 5 个被它标错的词，约 1 个是真人判错的；但真人判错的词里约 73% 能被它抓到。官方明确说这是 **calibrated heuristic**，不是用标注 learner 数据训练的模型。

### 1.5 适合/不适合

适合：

- 自托管语言学习产品、EdTech、研究；
- 需要“哪个音读错”的反馈，而不只是总分；
- 隐私敏感、不能把录音发云端的场景；
- 作为 PC 端参考/教师模型。

不适合：

- 直接塞进 MCU（2.4 GB 权重、Wav2Vec2-large、CPU 3 s/句）；
- 强口音/儿童/噪声场景（Wav2Vec2 主要在成人朗读数据上训练）；
- 把它的分数当成“语言能力评级”（官方也声明不是）。

---

## 第二部分：本地部署

### 2.1 本机环境

| 项 | 值 |
| --- | --- |
| Python | 3.12.4，`C:\venv-eink` |
| torch | 2.14.1+cpu |
| transformers | 5.17.0 |
| ffmpeg | 9.0.2 |
| eSpeak NG | 1.52.0（`C:\Program Files\eSpeak NG`，`winget install eSpeak-NG.eSpeak-NG`） |
| OpenPronounce | 0.3.0，editable 安装，源码在 `third_party/OpenPronounce` |
| 模型缓存 | `build/openpronounce/hf`（当前为空） |
| 服务 | `http://127.0.0.1:8000`，后台 uvicorn |

### 2.2 启动

```powershell
pwsh -File tools\openpronounce\start.ps1
```

然后打开 <http://127.0.0.1:8000>。脚本会设置：

```text
HF_HOME=<repo>\build\openpronounce\hf
PATH=C:\Program Files\eSpeak NG;...
```

### 2.3 已验证

```text
GET  /health      -> {"status":"ok"}
GET  /languages   -> en/fr/es/de/it/pt/nl
POST /phonemes    -> {"phonemes":["həloʊ","wɜːld"],"words":["hello","world"]}
GET  /            -> 200，页面标题 OpenPronounce
pytest            -> 99 passed in 5.40s
```

`/pronunciation` 需要两个 Wav2Vec2 权重。本机访问
`huggingface.co` 和 `hf-mirror.com` 都连接超时，权重未下载，所以该接口当前返回：

```json
{"detail":"Something went wrong"}
HTTP 500
```

日志是 `httpx.ConnectTimeout [WinError 10060]`，不是代码错误。

### 2.4 网络恢复后补全

第一次评分会自动下载约 2.4 GB：

```powershell
$env:HF_HOME="$PWD\build\openpronounce\hf"
$env:PATH="C:\Program Files\eSpeak NG;$env:PATH"
C:\venv-eink\Scripts\openpronounce.exe `
  third_party\OpenPronounce\assets\developer.wav "hello I am a developer"
```

或保持服务运行，直接：

```powershell
curl -X POST http://127.0.0.1:8000/pronunciation `
  -F "file=@third_party/OpenPronounce/assets/developer.wav" `
  -F "expected_text=hello I am a developer" -F "lang=en"
```

如果 HuggingFace 仍不通，可设置可达的镜像：

```powershell
$env:HF_ENDPOINT="https://<your-mirror>"
```

模型下载完后，`/pronunciation`、`/speech2text`、`/tts` 才完整可用。

### 2.5 本地 API

| 接口 | 表单 | 用途 |
| --- | --- | --- |
| `POST /pronunciation` | `file`, `expected_text`, `lang` | 完整评分/错误/韵律 |
| `POST /speech2text` | `file`, `lang` | 只转写 |
| `POST /phonemes` | `text`, `lang` | 文本→IPA |
| `POST /tts` | `text`, `lang` | 参考音（gTTS 需要网络） |
| `GET /health`, `/languages`, `/docs` | — | 健康/语言/Swagger |

---

## 第三部分：和本项目的关联

### 3.1 两者不是替代关系

| 维度 | 本项目（ESP32-S3 学习机） | OpenPronounce |
| --- | --- | --- |
| 运行位置 | 设备端 MCU，离线 | PC/服务器，CPU |
| 模型 | TinyMelNet **6,066 参数**，int8 **11.5 KB** | Wav2Vec2-large ×2，约 **2.4 GB** |
| 输入 | 2 s、40×64 log-Mel | 任意长度波形 + 期望文本 |
| 输出 | 二分类“通过/不通过”+ 概率 | 0–100 分 + 词/音素级错误 + 韵律 |
| 粒度 | 整词/整段 | **音素级** |
| 公开数据结果 | speechocean762 2000 条平衡子集：acc 0.697、AUC 0.759 | 500 条：Spearman 0.652、词级 recall 0.732/precision 0.196 |
| 成本 | 毫秒级、KB 级 | 秒级、GB 级 |
| 适合 | 离线、低成本、保护隐私的单词卡 | 高质量反馈、PC 参考、数据标注 |

两者都用了 **speechocean762**，但任务不同：本项目当前是“句级评分代理”的二分类，OpenPronounce 是“句级连续分 + 音素对齐”。所以 **69.7% 不能和 0.652 Spearman 直接比较**。

### 3.2 可以怎么用（按性价比排序）

1. **PC 端教师/上界**
   - 用设备采集的 600 条 WAV，跑 OpenPronounce，得到 score、PER、WER、词级错误；
   - 与设备 TinyMelNet 的输出做 Spearman / kappa / 阈值一致性；
   - 论文里可写“edge model vs PC phoneme-level reference”，比只报准确率更有说服力。

2. **弱标签/预标注**
   - OpenPronounce 先给“疑似读错的词 + 实际音素”，人工只做复核；
   - 但不能直接当 ground truth：词级 precision 0.196，误报多；
   - 适合减少标注时间，不适合替代双人标注/kappa。

3. **知识蒸馏/模型设计**
   - 用 OpenPronounce 的 score/phone error 作为 soft target，训练设备端小模型；
   - 但当前 TinyMelNet 是 **mel-only**，不知道目标词；要先做 **word-conditioned**（词 embedding 或参考 Mel 拼接），否则学不到“这个词应该怎么读”；
   - 可把 OpenPronounce 的 phone error 序列当作辅助任务，做多任务蒸馏；这是 M5 之后的研究点，不是 M2 能直接上的。

4. **规则法对照**
   - OpenPronounce 的 acoustic DTW + PER/WER 可以作为规则法 F0+DTW 的“更强参考”；
   - 但它是学习型音素识别，不是纯信号规则；论文中要分清“规则法”“小 CNN”“PC 大模型”三条基线。

5. **在线/离线混合（不推荐作为主线）**
   - 设备录音 → Wi-Fi 上传 PC → OpenPronounce → 回传分数；
   - 能拿到最好反馈，但违背“边缘 AI/离线”的课题定位，也引入网络/隐私/延迟问题；
   - 只能作为演示或对照，不作为最终产品形态。

### 3.3 不能直接部署到设备的原因

- 权重 2.4 GB，ESP32-S3 只有 16 MB Flash + 8 MB PSRAM；
- Wav2Vec2-large 约 3 亿参数，单次 CPU 3 s；ESP32 算力差 3–4 个数量级；
- 需要 torch/transformers/espeak，固件是 C/FreeRTOS；
- 量化到 int8 也仍是几百 MB，远超设备。

### 3.4 建议的论文落点

- **不要**把 OpenPronounce 当成“本项目的模型”；
- 把它写成 **PC 端音素级参考基线**，用于：
  - 验证设备端小模型是否学到发音特征而不是说话人/信道捷径；
  - 给自采数据做弱预标注，再人工复核；
  - 分析设备模型与音素级指标的差异（例如设备对 /θ/、/ð/ 的盲区）。
- 设备端仍坚持 TinyMelNet + log-Mel + int8；OpenPronounce 只进入“对照实验/数据分析”，不进入固件。

### 3.5 许可与合规

- OpenPronounce 代码 MIT，可参考/引用；模型权重有各自 license，不要直接把权重提交进本仓库；
- 本项目 partial LUT 仍是 AGPL 来源，和 OpenPronounce 的 MIT 不冲突，但发布前仍要处理 LUT（见 `THIRD_PARTY_NOTICES.md`）；
- 自采录音只存匿名 `S01..S08`，不要上传到公共 HuggingFace/模型服务。

---

## 结论

- **研究**：OpenPronounce 是目前少见的、MIT、可自托管、音素级发音评测实现；强项是“哪个音读错”，弱项是 2.4 GB 权重、词级误报偏高、强口音/儿童场景退化。
- **部署**：本地 Web/API 已跑通（`/health`、`/languages`、`/phonemes`、UI、99 个测试）；完整评分因 HuggingFace 权重下载超时暂不可用，网络恢复后自动补全。
- **关联**：它是本项目的 **PC 参考/教师/弱标注工具**，不是设备端替代品；最有价值的是用它的音素级输出校准和解释 TinyMelNet，而不是把 Wav2Vec2 塞进 ESP32。
