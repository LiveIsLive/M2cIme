#include "pch.h"
#include "PinyinSegmenter.h"

#include <unordered_set>

namespace
{
    // 汉语拼音全部合法音节（不含声调）。
    // 注意：v 用来替代 ü（如 nv=nü, lv=lü），以符合常见输入法习惯。
    const char* kSyllables[] = {
        "a","ai","an","ang","ao",
        "ba","bai","ban","bang","bao","bei","ben","beng","bi","bian","biao","bie","bin","bing","bo","bu",
        "ca","cai","can","cang","cao","ce","cei","cen","ceng","ci","cong","cou","cu","cuan","cui","cun","cuo",
        "cha","chai","chan","chang","chao","che","chen","cheng","chi","chong","chou","chu","chua","chuai",
        "chuan","chuang","chui","chun","chuo",
        "da","dai","dan","dang","dao","de","dei","den","deng","di","dia","dian","diao","die","ding","diu",
        "dong","dou","du","duan","dui","dun","duo",
        "e","ei","en","er",
        "fa","fan","fang","fei","fen","feng","fo","fou","fu",
        "ga","gai","gan","gang","gao","ge","gei","gen","geng","gong","gou","gu","gua","guai","guan",
        "guang","gui","gun","guo",
        "ha","hai","han","hang","hao","he","hei","hen","heng","hng","hm","hong","hou","hu","hua","huai",
        "huan","huang","hui","hun","huo",
        "ji","jia","jian","jiang","jiao","jie","jin","jing","jiong","jiu","ju","juan","jue","jun",
        "ka","kai","kan","kang","kao","ke","kei","ken","keng","kong","kou","ku","kua","kuai","kuan",
        "kuang","kui","kun","kuo",
        "la","lai","lan","lang","lao","le","lei","leng","li","lia","lian","liang","liao","lie","lin",
        "ling","liu","long","lou","lu","lv","luan","lue","lun","luo",
        "ma","mai","man","mang","mao","me","mei","men","meng","mi","mian","miao","mie","min","ming",
        "miu","mo","mou","mu",
        "na","nai","nan","nang","nao","ne","nei","nen","neng","ng","ni","nian","niang","niao","nie","nin",
        "ning","niu","nong","nou","nu","nv","nuan","nue","nuo",
        "o","ou",
        "pa","pai","pan","pang","pao","pei","pen","peng","pi","pian","piao","pie","pin","ping","po","pou","pu",
        "qi","qia","qian","qiang","qiao","qie","qin","qing","qiong","qiu","qu","quan","que","qun",
        "ran","rang","rao","re","ren","reng","ri","rong","rou","ru","rua","ruan","rui","run","ruo",
        "sa","sai","san","sang","sao","se","sen","seng","si","song","sou","su","suan","sui","sun","suo",
        "sha","shai","shan","shang","shao","she","shei","shen","sheng","shi","shou","shu","shua","shuai",
        "shuan","shuang","shui","shun","shuo",
        "ta","tai","tan","tang","tao","te","tei","teng","ti","tian","tiao","tie","ting","tong","tou","tu",
        "tuan","tui","tun","tuo",
        "wa","wai","wan","wang","wei","wen","weng","wo","wu",
        "xi","xia","xian","xiang","xiao","xie","xin","xing","xiong","xiu","xu","xuan","xue","xun",
        "ya","yan","yang","yao","ye","yi","yin","ying","yo","yong","you","yu","yuan","yue","yun",
        "za","zai","zan","zang","zao","ze","zei","zen","zeng","zi","zong","zou","zu","zuan","zui","zun","zuo",
        "zha","zhai","zhan","zhang","zhao","zhe","zhei","zhen","zheng","zhi","zhong","zhou","zhu","zhua",
        "zhuai","zhuan","zhuang","zhui","zhun","zhuo"
    };

    const std::vector<std::string>& SyllableList()
    {
        static const std::vector<std::string> list(std::begin(kSyllables), std::end(kSyllables));
        return list;
    }

    const std::unordered_set<std::string>& SyllableSet()
    {
        static const std::unordered_set<std::string> set(std::begin(kSyllables), std::end(kSyllables));
        return set;
    }
}

const std::vector<std::string>& PinyinSegmenter::GetAllSyllables()
{
    return SyllableList();
}

bool PinyinSegmenter::IsValidSyllable(const std::string& s)
{
    return SyllableSet().find(s) != SyllableSet().end();
}

std::vector<std::string> PinyinSegmenter::Segment(const std::string& input, std::string* pRemainder)
{
    std::vector<std::string> result;
    const auto& set = SyllableSet();

    size_t i = 0;
    const size_t n = input.size();
    while (i < n)
    {
        // 正向最大匹配：从最长 6 个字符（拼音最长音节长度）尝试
        size_t bestLen = 0;
        for (size_t len = 6; len >= 1; --len)
        {
            if (i + len > n) continue;
            std::string sub = input.substr(i, len);
            if (set.find(sub) != set.end())
            {
                bestLen = len;
                break;
            }
        }
        if (bestLen == 0)
        {
            // 剩余部分无法构成合法音节
            if (pRemainder) *pRemainder = input.substr(i);
            return result;
        }
        result.push_back(input.substr(i, bestLen));
        i += bestLen;
    }

    if (pRemainder) pRemainder->clear();
    return result;
}

std::string PinyinSegmenter::Join(const std::vector<std::string>& syllables)
{
    std::string out;
    for (const auto& s : syllables) out += s;
    return out;
}
