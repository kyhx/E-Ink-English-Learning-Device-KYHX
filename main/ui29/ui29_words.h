/* 由 tools/ui29_assets.py 生成，不要手改。
 * 数据来源：data/words.csv（50 词，含 IPA）。
 * SPDX-License-Identifier: MIT
 */
#ifndef UI29_WORDS_H
#define UI29_WORDS_H

typedef struct {
    const char *word;
    const char *ipa;
    const char *meaning;
} ui29_word_t;

static const ui29_word_t k_words29[] = {
    {"apple", "/ˈæp.əl/", "苹果"},
    {"book", "/bʊk/", "书"},
    {"cat", "/kæt/", "猫"},
    {"dog", "/dɒg/", "狗"},
    {"eat", "/iːt/", "吃"},
    {"fish", "/fɪʃ/", "鱼"},
    {"good", "/gʊd/", "好"},
    {"hand", "/hænd/", "手"},
    {"idea", "/aɪˈdɪə/", "想法"},
    {"jump", "/dʒʌmp/", "跳"},
    {"kind", "/kaɪnd/", "善良的"},
    {"love", "/lʌv/", "爱"},
    {"moon", "/muːn/", "月亮"},
    {"name", "/neɪm/", "名字"},
    {"open", "/ˈəʊpən/", "打开"},
    {"park", "/pɑːk/", "公园"},
    {"quiz", "/kwɪz/", "测验"},
    {"rain", "/reɪn/", "雨"},
    {"sun", "/sʌn/", "太阳"},
    {"tree", "/triː/", "树"},
    {"unit", "/ˈjuːnɪt/", "单元"},
    {"very", "/ˈveri/", "非常"},
    {"word", "/wɜːd/", "单词"},
    {"yard", "/jɑːd/", "院子"},
    {"zero", "/ˈzɪərəʊ/", "零"},
    {"bird", "/bɜːd/", "鸟"},
    {"blue", "/bluː/", "蓝色"},
    {"cold", "/kəʊld/", "冷"},
    {"door", "/dɔː/", "门"},
    {"easy", "/ˈiːzi/", "容易的"},
    {"face", "/feɪs/", "脸"},
    {"game", "/geɪm/", "游戏"},
    {"home", "/həʊm/", "家"},
    {"iron", "/ˈaɪən/", "铁"},
    {"just", "/dʒʌst/", "只是"},
    {"keep", "/kiːp/", "保持"},
    {"light", "/laɪt/", "光"},
    {"move", "/muːv/", "移动"},
    {"nice", "/naɪs/", "好的"},
    {"only", "/ˈəʊnli/", "只有"},
    {"play", "/pleɪ/", "玩"},
    {"quick", "/kwɪk/", "快的"},
    {"read", "/riːd/", "读"},
    {"ship", "/ʃɪp/", "船"},
    {"time", "/taɪm/", "时间"},
    {"upon", "/əˈpɒn/", "在...上"},
    {"view", "/vjuː/", "看法"},
    {"warm", "/wɔːm/", "温暖的"},
    {"year", "/jɪə/", "年"},
    {"zoom", "/zuːm/", "快速移动"},
};

#define UI29_WORD_COUNT ((int)(sizeof(k_words29) / sizeof(k_words29[0])))

#endif /* UI29_WORDS_H */
