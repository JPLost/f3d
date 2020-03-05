#include "F3DOptions.h"

#include "F3DLog.h"

#include <vtk_jsoncpp.h>

#include <vtksys/SystemTools.hxx>

#include <fstream>
#include <regex>
#include <vector>

#include "cxxopts.hpp"

class ConfigurationOptions
{
public:
  ConfigurationOptions(int argc, char** argv)
    : Argc(argc)
    , Argv(argv)
  {
  }

  F3DOptions GetOptionsFromArgs(std::vector<std::string>& inputs);
  bool InitializeDictionaryFromConfigFile(const std::string& filePath);

protected:
  template<class T>
  std::string GetOptionDefault(const std::string& option, T currValue) const
  {
    auto it = this->ConfigDic.find(option);
    if (it == this->ConfigDic.end())
    {
      std::stringstream ss;
      ss << currValue;
      return ss.str();
    }
    return it->second;
  }

  std::string GetOptionDefault(const std::string& option, bool currValue) const
  {
    auto it = this->ConfigDic.find(option);
    if (it == this->ConfigDic.end())
    {
      return currValue ? "true" : "false";
    }
    return it->second;
  }

  std::string CollapseName(const std::string& longName, const std::string& shortName) const
  {
    std::stringstream ss;
    if (shortName != "")
    {
      ss << shortName << ",";
    }
    ss << longName;
    return ss.str();
  }

  template<class T>
  std::string GetOptionDefault(const std::string& option, const std::vector<T>& currValue) const
  {
    auto it = this->ConfigDic.find(option);
    if (it == this->ConfigDic.end())
    {
      std::stringstream ss;
      for (size_t i = 0; i < currValue.size(); i++)
      {
        ss << currValue[i];
        if (i != currValue.size() - 1)
          ss << ",";
      }
      return ss.str();
    }
    return it->second;
  }

  void DeclareOption(cxxopts::OptionAdder& group, const std::string& longName,
    const std::string& shortName, const std::string& doc) const
  {
    group(this->CollapseName(longName, shortName), doc);
  }

  template<class T>
  void DeclareOption(cxxopts::OptionAdder& group, const std::string& longName,
    const std::string& shortName, const std::string& doc, T& var, bool hasDefault = true,
    const std::string& argHelp = "") const
  {
    auto val = cxxopts::value<T>(var);
    if (hasDefault)
    {
      val = val->default_value(this->GetOptionDefault(longName, var));
    }
    var = {};
    group(this->CollapseName(longName, shortName), doc, val, argHelp);
  }

  template<class T>
  void DeclareOption(cxxopts::OptionAdder& group, const std::string& longName,
    const std::string& shortName, const std::string& doc, T& var, const std::string& implicitValue,
    const std::string& argHelp = "") const
  {
    auto val = cxxopts::value<T>(var)->implicit_value(implicitValue);
    var = {};
    group(this->CollapseName(longName, shortName), doc, val, argHelp);
  }

  static std::string GetUserSettingsDirectory();
  static std::string GetUserSettingsFilePath();

protected:
  int Argc;
  char** Argv;

  using Dictionnary = std::map<std::string, std::string>;
  Dictionnary ConfigDic;
};

//----------------------------------------------------------------------------
F3DOptions ConfigurationOptions::GetOptionsFromArgs(std::vector<std::string>& inputs)
{
  F3DOptions options;
  try
  {
    cxxopts::Options cxxOptions(f3d::AppName, f3d::AppTitle);
    cxxOptions.positional_help("file1 file2 ...");

    auto grp1 = cxxOptions.add_options();
    this->DeclareOption(grp1, "input", "", "Input file", inputs, false, "<files>");
    this->DeclareOption(grp1, "output", "", "Render to file", options.Output, false, "<png file>");
    this->DeclareOption(grp1, "help", "h", "Print help");
    this->DeclareOption(grp1, "version", "", "Print version details");
    this->DeclareOption(grp1, "verbose", "v", "Enable verbose mode", options.Verbose);
    this->DeclareOption(grp1, "axis", "x", "Show axes", options.Axis);
    this->DeclareOption(grp1, "grid", "g", "Show grid", options.Grid);
    this->DeclareOption(grp1, "edges", "e", "Show cell edges", options.Edges);
    this->DeclareOption(grp1, "progress", "", "Show progress bar", options.Progress);
    this->DeclareOption(grp1, "geometry-only", "m",
      "Do not read materials, cameras and lights from file", options.GeometryOnly);

    auto grp2 = cxxOptions.add_options("Material");
    this->DeclareOption(grp2, "point-sprites", "o", "Show sphere sprites instead of geometry", options.PointSprites);
    this->DeclareOption(grp2, "point-size", "", "Point size when showing vertices or point sprites", options.PointSize, true, "<size>");
    this->DeclareOption(grp2, "color", "", "Solid color", options.SolidColor, true, "<R,G,B>");
    this->DeclareOption(grp2, "opacity", "", "Opacity", options.Opacity, true, "<opacity>");
    this->DeclareOption(grp2, "roughness", "", "Roughness coefficient (0.0-1.0)", options.Roughness,
      true, "<roughness>");
    this->DeclareOption(
      grp2, "metallic", "", "Metallic coefficient (0.0-1.0)", options.Metallic, true, "<metallic>");

    auto grp3 = cxxOptions.add_options("Window");
    this->DeclareOption(
      grp3, "bg-color", "", "Background color", options.BackgroundColor, true, "<R,G,B>");
    this->DeclareOption(
      grp3, "resolution", "", "Window resolution", options.WindowSize, true, "<width,height>");
    this->DeclareOption(grp3, "timer", "t", "Display frame per second", options.FPS);
    this->DeclareOption(grp3, "filename", "n", "Display filename", options.Filename);

    auto grp4 = cxxOptions.add_options("Scientific visualization");
    this->DeclareOption(grp4, "scalars", "s", "Color by scalars", options.Scalars,
      std::string("f3d_reserved"), "<array_name>");
    this->DeclareOption(grp4, "comp", "", "Component from the scalar array to color with",
      options.Component, true, "<comp_index>");
    this->DeclareOption(grp4, "cells", "c", "Use a scalar array from the cells", options.Cells);
    this->DeclareOption(grp4, "range", "", "Custom range for the coloring by array", options.Range,
      false, "<min,max>");
    this->DeclareOption(grp4, "bar", "b", "Show scalar bar", options.Bar);

#if F3D_HAS_RAYTRACING
    auto grp5 = cxxOptions.add_options("Raytracing");
    this->DeclareOption(grp5, "raytracing", "r", "Enable raytracing", options.Raytracing);
    this->DeclareOption(
      grp5, "samples", "", "Number of samples per pixel", options.Samples, true, "<samples>");
    this->DeclareOption(grp5, "denoise", "d", "Denoise the image", options.Denoise);
#endif

    auto grp6 = cxxOptions.add_options("PostFX (OpenGL)");
    this->DeclareOption(grp6, "depth-peeling", "p", "Enable depth peeling", options.DepthPeeling);
    this->DeclareOption(grp6, "ssao", "u", "Enable Screen-Space Ambient Occlusion", options.SSAO);
    this->DeclareOption(grp6, "fxaa", "f", "Enable Fast Approximate Anti-Aliasing", options.FXAA);

    auto grp7 = cxxOptions.add_options("Testing");
    this->DeclareOption(grp7, "ref", "", "Reference", options.Reference, false, "<png file>");
    this->DeclareOption(
      grp7, "ref-threshold", "", "Testing threshold", options.RefThreshold, false, "<threshold>");

    cxxOptions.parse_positional({ "input" });

    int argc = this->Argc;
    auto result = cxxOptions.parse(argc, this->Argv);

    if (result.count("help") > 0)
    {
      F3DLog::Print(F3DLog::Severity::Info, cxxOptions.help());
      F3DLog::Print(F3DLog::Severity::Info,
                    "Keys:\n"
                    " ESC       Exit f3d\n"
                    " RETURN    Reset camera zoom\n"
                    " x         Toggle the axes display\n"
                    " g         Toggle the grid display\n"
                    " e         Toggle the edges display\n"
                    " s         Toggle the coloration by scalar\n"
                    " b         Toggle the scalar bar display\n"
                    " t         Toggle the FPS counter display\n"
                    " n         Toggle the filename display\n"
                    " r         Toggle raytracing rendering\n"
                    " d         Toggle denoising when raytracing\n"
                    " p         Toggle depth peeling\n"
                    " u         Toggle SSAO\n"
                    " f         Toggle FXAA\n"
                    " o         Toggle point sprites rendering\n");
      exit(EXIT_SUCCESS);
    }

    if (result.count("version") > 0)
    {
      std::string version = f3d::AppTitle;
      version += "\nVersion: ";
      version += f3d::AppVersion;
      version += "\nBuild date: ";
      version += f3d::AppBuildDate;
      version += "\nRayTracing module: ";
#if F3D_HAS_RAYTRACING
      version += "ON";
#else
      version += "OFF";
#endif
      version += "\nAuthor: Kitware SAS";

      F3DLog::Print(F3DLog::Severity::Info, version);
      exit(EXIT_SUCCESS);
    }
  }
  catch (const cxxopts::OptionException& e)
  {
    F3DLog::Print(F3DLog::Severity::Error, "Error parsing options: ", e.what());
    exit(EXIT_FAILURE);
  }
  return options;
}

//----------------------------------------------------------------------------
bool ConfigurationOptions::InitializeDictionaryFromConfigFile(const std::string& filePath)
{
  const std::string& configFilePath = this->GetUserSettingsFilePath();
  std::ifstream file;
  file.open(configFilePath.c_str());

  if (!file.is_open())
  {
    return false;
  }

  Json::Value root;
  Json::CharReaderBuilder builder;
  builder["collectComments"] = false;
  std::string errs;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  bool success = Json::parseFromStream(builder, file, &root, &errs);
  if (!success)
  {
    F3DLog::Print(
      F3DLog::Severity::Error, "Unable to parse the configuration file ", configFilePath);
    F3DLog::Print(F3DLog::Severity::Error, errs);
    return false;
  }

  for (auto const& id : root.getMemberNames())
  {
    std::regex re(id);
    std::smatch matches;
    if (std::regex_match(filePath, matches, re))
    {
      const Json::Value node = root[id];

      for (auto const& nl : node.getMemberNames())
      {
        const Json::Value v = node[nl];
        this->ConfigDic[nl] = v.asString();
      }
    }
  }

  return true;
}

//----------------------------------------------------------------------------
std::string ConfigurationOptions::GetUserSettingsDirectory()
{
  std::string applicationName = "f3d";
#if defined(_WIN32)
  const char* appData = vtksys::SystemTools::GetEnv("APPDATA");
  if (!appData)
  {
    return std::string();
  }
  std::string separator("\\");
  std::string directoryPath(appData);
  if (directoryPath[directoryPath.size() - 1] != separator[0])
  {
    directoryPath.append(separator);
  }
  directoryPath += applicationName + separator;
#else
  std::string directoryPath;
  std::string separator("/");

  // Emulating QSettings behavior.
  const char* xdgConfigHome = vtksys::SystemTools::GetEnv("XDG_CONFIG_HOME");
  if (xdgConfigHome && strlen(xdgConfigHome) > 0)
  {
    directoryPath = xdgConfigHome;
    if (directoryPath[directoryPath.size() - 1] != separator[0])
    {
      directoryPath += separator;
    }
  }
  else
  {
    const char* home = vtksys::SystemTools::GetEnv("HOME");
    if (!home)
    {
      return std::string();
    }
    directoryPath = home;
    if (directoryPath[directoryPath.size() - 1] != separator[0])
    {
      directoryPath += separator;
    }
    directoryPath += ".config/";
  }
  directoryPath += applicationName + separator;
#endif
  return directoryPath;
}

//----------------------------------------------------------------------------
std::string ConfigurationOptions::GetUserSettingsFilePath()
{
  return ConfigurationOptions::GetUserSettingsDirectory() + "f3d.json";
}

//----------------------------------------------------------------------------
F3DOptionsParser::F3DOptionsParser() = default;

//----------------------------------------------------------------------------
F3DOptionsParser::~F3DOptionsParser() = default;

//----------------------------------------------------------------------------
void F3DOptionsParser::Initialize(int argc, char** argv)
{
  this->ConfigOptions = std::unique_ptr<ConfigurationOptions>(new ConfigurationOptions(argc, argv));
}

//----------------------------------------------------------------------------
F3DOptions F3DOptionsParser::GetOptionsFromCommandLine(std::vector<std::string>& files)
{
  return this->ConfigOptions->GetOptionsFromArgs(files);
}

//----------------------------------------------------------------------------
F3DOptions F3DOptionsParser::GetOptionsFromFile(const std::string& filePath)
{
  this->ConfigOptions->InitializeDictionaryFromConfigFile(filePath);

  std::vector<std::string> dummy;
  F3DOptions options = this->GetOptionsFromCommandLine(dummy);

  // Check the validity of the options
  if (options.Verbose)
  {
    F3DOptionsParser::CheckValidity(options, filePath);
  }

  return options;
}

//----------------------------------------------------------------------------
bool F3DOptionsParser::CheckValidity(const F3DOptions& options, const std::string& filePath)
{
  bool ret = true;
  F3DOptions defaultOptions;
  bool usingDefaultScene = true;

  if (!options.GeometryOnly)
  {
    std::string ext = vtksys::SystemTools::GetFilenameLastExtension(filePath);
    ext = vtksys::SystemTools::LowerCase(ext);

    if (ext == ".3ds" || ext == ".obj" || ext == ".wrl" || ext == ".gltf" || ext == ".glb")
    {
      usingDefaultScene = false;
    }
  }

  if (!usingDefaultScene)
  {
    if (defaultOptions.PointSprites != options.PointSprites)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying to show shere sprites while not using the default scene has no effect.");
      ret = false;
    }
    if (defaultOptions.SolidColor != options.SolidColor)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying a Solid Color while not using the default scene has no effect.");
      ret = false;
    }
    if (defaultOptions.Opacity != options.Opacity)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying an Opacity while not using the default scene has no effect.");
      ret = false;
    }
    if (defaultOptions.Roughness != options.Roughness)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying a Roughness coefficient while not using the default scene has no effect.");
      ret = false;
    }
    if (defaultOptions.Metallic != options.Metallic)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying a Metallic coefficient while not using the default scene has no effect.");
      ret = false;
    }
    if (defaultOptions.Scalars != options.Scalars)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying Scalars to color with while not using the default scene has no effect.");
      ret = false;
    }
    if (defaultOptions.Component != options.Component)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying a Component to color with while not using the default scene has no effect.");
      ret = false;
    }
    if (defaultOptions.Cells != options.Cells)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying to color with Cells while not using the default scene has no effect.");
      ret = false;
    }
    if (defaultOptions.Range != options.Range)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying a Range to color with while not using the default scene has no effect.");
      ret = false;
    }
    if (defaultOptions.Bar != options.Bar)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying to show a scalar Bar while not using the default scene has no effect.");
      ret = false;
    }
  }
  else
  {
    if (defaultOptions.Scalars == options.Scalars)
    {
      if (defaultOptions.Component != options.Component)
      {
        F3DLog::Print(F3DLog::Severity::Info, "Specifying a Component to color with has no effect without specifying Scalars to color with.");
        ret = false;
      }
      if (defaultOptions.Cells != options.Cells)
      {
        F3DLog::Print(F3DLog::Severity::Info, "Specifying to color with Cells has no effect without specifying Scalars to color with.");
        ret = false;
      }
      if (defaultOptions.Range != options.Range)
      {
        F3DLog::Print(F3DLog::Severity::Info, "Specifying a Range to color with has no effect without specifying Scalars to color with.");
        ret = false;
      }
      if (defaultOptions.Bar != options.Bar)
      {
        F3DLog::Print(F3DLog::Severity::Info, "Specifying to show a scalar Bar has no effect without specifying Scalars to color with.");
        ret = false;
      }
    }
  }

  if (options.Raytracing)
  {
    if(defaultOptions.PointSprites != options.PointSprites)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying to show point sprites has no effect when using Raytracing.");
      ret = false;
    }
    if(defaultOptions.FPS != options.FPS)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying to display the Frame per second counter has no effect when using Raytracing.");
      ret = false;
    }
    if(defaultOptions.DepthPeeling != options.DepthPeeling)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying to render using Depth Peeling has no effect when using Raytracing.");
      ret = false;
    }
    if(defaultOptions.FXAA != options.FXAA)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying to render using FXAA has no effect when using Raytracing.");
      ret = false;
    }
    if(defaultOptions.SSAO != options.SSAO)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying to render using SSAO has no effect when using Raytracing.");
      ret = false;
    }
  }
  else
  {
    if(defaultOptions.Samples != options.Samples)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying a Number of samples per pixel has no effect when not using Raytracing.");
      ret = false;
    }
    if(defaultOptions.Denoise != options.Denoise)
    {
      F3DLog::Print(F3DLog::Severity::Info, "Specifying to Denoise the image has no effect when not using Raytracing.");
      ret = false;
    }
    if(defaultOptions.PointSprites == options.PointSprites)
    {
      if (defaultOptions.Opacity != options.Opacity)
      {
        F3DLog::Print(F3DLog::Severity::Info, "Specifying an Opacity while using point sprites has no effect.");
        ret = false;
      }
      if (defaultOptions.Roughness != options.Roughness)
      {
        F3DLog::Print(F3DLog::Severity::Info, "Specifying a Roughness coefficient while using point sprites has no effect.");
        ret = false;
      }
      if (defaultOptions.Metallic != options.Metallic)
      {
        F3DLog::Print(F3DLog::Severity::Info, "Specifying a Metallic coefficient while using point sprites has no effect.");
        ret = false;
      }
    }
  }
  return ret;
}