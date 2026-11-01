#pragma once
#include "card_layout.hpp"
#include <map>

namespace t6card {
struct Json {
    enum Kind { Object, Array, String, Scalar } kind=Scalar;
    std::map<std::string,Json> members;
    std::vector<Json> items;
    std::string text;
    const Json& get(const std::string& key) const {
        require(kind==Object,"manifest value is not an object");
        const auto it=members.find(key);require(it!=members.end(),"required manifest field missing");return it->second;
    }
    std::string string() const {require(kind==String,"manifest value is not a string");return text;}
};
class JsonParser {
    const std::string& s; size_t p=0, nodes=0;
    void ws(){while(p<s.size() && (s[p]==' '||s[p]=='\n'||s[p]=='\r'||s[p]=='\t')) ++p;}
    char take(){require(p<s.size(),"truncated manifest JSON");return s[p++];}
    std::string string(){
        require(take()=='"',"expected JSON string");std::string out;
        while(true){char c=take();if(c=='"') return out;
            require(static_cast<unsigned char>(c)>=32,"control character in JSON string");
            if(c=='\\'){c=take();switch(c){
                case '"':case '\\':case '/':break;
                case 'b':c='\b';break;case 'f':c='\f';break;case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;
                case 'u':{unsigned n=0;for(int i=0;i<4;i++){char h=take();require((h>='0'&&h<='9')||(h>='a'&&h<='f')||(h>='A'&&h<='F'),"bad JSON unicode escape");n=n*16+unsigned(h<='9'?h-'0':(h>='a'?h-'a':h-'A')+10);}require(n>=32&&n<128,"non-ASCII unicode escape unsupported in manifest");c=static_cast<char>(n);break;}
                default:require(false,"bad JSON escape");}}
            out+=c;require(out.size()<=32768,"manifest string too long");}
    }
    Json value(unsigned depth){
        require(depth<=32 && ++nodes<=20000,"manifest JSON nesting/node limit");ws();require(p<s.size(),"missing JSON value");Json j;
        if(s[p]=='{'){j.kind=Json::Object;++p;ws();if(p<s.size()&&s[p]=='}'){++p;return j;}
            while(true){ws();std::string key=string();ws();require(take()==':',"missing JSON colon");
                Json child=value(depth+1);require(j.members.emplace(key,std::move(child)).second,"duplicate JSON key");ws();char c=take();if(c=='}')break;require(c==',',"missing JSON comma");}
        }else if(s[p]=='['){j.kind=Json::Array;++p;ws();if(p<s.size()&&s[p]==']'){++p;return j;}
            while(true){j.items.push_back(value(depth+1));ws();char c=take();if(c==']')break;require(c==',',"missing JSON array comma");}
        }else if(s[p]=='"'){j.kind=Json::String;j.text=string();}
        else {const size_t start=p;while(p<s.size()&&s[p]!=','&&s[p]!=']'&&s[p]!='}'&&s[p]!=' '&&s[p]!='\n'&&s[p]!='\r'&&s[p]!='\t')++p;
            j.text=s.substr(start,p-start);require(j.text=="true"||j.text=="false"||j.text=="null"||number(j.text),"invalid JSON scalar");}
        return j;
    }
    static bool number(const std::string& n){size_t i=0;if(i<n.size()&&n[i]=='-')++i;if(i==n.size())return false;
        if(n[i]=='0')++i;else{if(n[i]<'1'||n[i]>'9')return false;while(i<n.size()&&n[i]>='0'&&n[i]<='9')++i;}
        if(i<n.size()&&n[i]=='.'){++i;const size_t a=i;while(i<n.size()&&n[i]>='0'&&n[i]<='9')++i;if(a==i)return false;}
        if(i<n.size()&&(n[i]=='e'||n[i]=='E')){++i;if(i<n.size()&&(n[i]=='+'||n[i]=='-'))++i;const size_t a=i;while(i<n.size()&&n[i]>='0'&&n[i]<='9')++i;if(a==i)return false;}return i==n.size();}
public:
    explicit JsonParser(const std::string& input):s(input){}
    Json parse(){require(s.size()<=2*1024*1024,"manifest exceeds 2 MiB");Json j=value(0);ws();require(p==s.size(),"trailing JSON data");return j;}
};
inline bool hex_hash(const std::string& s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}
inline std::string package_autoexec_hash(const std::string& manifest) {
    const Json j=JsonParser(manifest).parse();
    require(j.get("canon_firmware").string()=="1.1.0" && j.get("platform").string()=="1300D.110","package is not T6 firmware 1.1.0");
    require(j.get("magic_lantern_commit").string()=="142472e95054d73640f50789ac0fd66325499d82","unexpected Magic Lantern source pin");
    const std::string hash=j.get("artifact_hashes").get("AUTOEXEC.BIN").string();require(hex_hash(hash),"invalid AUTOEXEC hash in manifest");return hash;
}
} // namespace t6card
