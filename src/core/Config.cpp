#include "Config.h"
#include <fstream>

namespace fluxus {
Config&Config::instance(){static Config config;return config;}
void Config::setDataDirectory(const std::filesystem::path&path){{std::lock_guard lock(mutex_);dataDirectory_=path;}std::filesystem::create_directories(downloadedLogsDirectory());load();}
void Config::load(){std::lock_guard lock(mutex_);values_=Json{{"ssh_key_path",""},{"default_device_password",""},{"theme","dark"},{"devices",Json::array()},{"last_window_geometry",Json::array()},{"auto_check_updates",true}};std::ifstream input(dataDirectory_/"config.json");if(input)try{values_.update(Json::parse(input));}catch(...){} }
void Config::save(){Json copy;std::filesystem::path directory;{std::lock_guard lock(mutex_);copy=values_;directory=dataDirectory_;}std::filesystem::create_directories(directory);const auto path=directory/"config.json",temp=directory/"config.json.tmp";{std::ofstream output(temp,std::ios::binary|std::ios::trunc);if(!output)throw std::runtime_error("Не удалось сохранить config.json");output<<copy.dump(2);}std::error_code error;std::filesystem::rename(temp,path,error);if(error){std::filesystem::remove(path);std::filesystem::rename(temp,path);}}
Json Config::get(const std::string&key,const Json&fallback)const{std::lock_guard lock(mutex_);return values_.contains(key)?values_[key]:fallback;}
void Config::set(const std::string&key,const Json&value,bool saveNow){{std::lock_guard lock(mutex_);values_[key]=value;}if(saveNow)save();}
std::string Config::string(const std::string&key,const std::string&fallback)const{const auto value=get(key);return value.is_string()?value.get<std::string>():fallback;}
bool Config::boolean(const std::string&key,bool fallback)const{const auto value=get(key);return value.is_boolean()?value.get<bool>():fallback;}
std::vector<DeviceConnection>Config::devices()const{std::vector<DeviceConnection>result;const auto value=get("devices",Json::array());if(value.is_array())for(const auto&item:value){DeviceConnection d;d.id=item.value("id","");d.host=item.value("host","");d.port=item.value("port",22);d.username=item.value("username","root");d.password=item.value("password","");d.comment=item.value("comment","");if(!d.host.empty())result.push_back(std::move(d));}return result;}
void Config::setDevices(const std::vector<DeviceConnection>&devices,bool saveNow){Json value=Json::array();for(const auto&d:devices)value.push_back({{"id",d.id},{"host",d.host},{"port",d.port},{"username",d.username},{"password",d.password},{"comment",d.comment}});set("devices",value,saveNow);}
} // namespace fluxus
