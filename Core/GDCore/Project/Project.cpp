/*
 * GDevelop Core
 * Copyright 2008-2016 Florian Rival (Florian.Rival@gmail.com). All rights
 * reserved. This project is released under the MIT License.
 */

#include "Project.h"

#include <stdio.h>
#include <stdlib.h>

#include <cctype>
#include <fstream>
#include <map>
#include <vector>

#include "GDCore/CommonTools.h"
#include "GDCore/Events/Parsers/GrammarTerminals.h"
#include "GDCore/Extensions/Metadata/ExpressionMetadata.h"
#include "GDCore/Extensions/Metadata/MetadataProvider.h"
#include "GDCore/Extensions/Platform.h"
#include "GDCore/Extensions/PlatformExtension.h"
#include "GDCore/IDE/PlatformManager.h"
#include "GDCore/Project/CustomObjectConfiguration.h"
#include "GDCore/Project/EventsFunctionsExtension.h"
#include "GDCore/Project/ExternalEvents.h"
#include "GDCore/Project/ExternalLayout.h"
#include "GDCore/Project/Layout.h"
#include "GDCore/Project/Object.h"
#include "GDCore/Project/ObjectConfiguration.h"
#include "GDCore/Project/ObjectGroupsContainer.h"
#include "GDCore/Project/ResourcesManager.h"
#include "GDCore/Serialization/Serializer.h"
#include "GDCore/Serialization/SerializerElement.h"
#include "GDCore/String.h"
#include "GDCore/Tools/Localization.h"
#include "GDCore/Tools/Log.h"
#include "GDCore/Tools/PolymorphicClone.h"
#include "GDCore/Tools/UUID/UUID.h"
#include "GDCore/Tools/VersionWrapper.h"
#include "GDCore/Utf8/utf8.h"

using namespace std;

#undef CreateEvent

namespace gd {

Project::Project()
    : name(_("Project")),
      version("1.0.0"),
      packageName("com.example.gamename"),
      templateSlug(""),
      orientation("landscape"),
      folderProject(false),
      windowWidth(800),
      windowHeight(600),
      maxFPS(60),
      minFPS(20),
      verticalSync(false),
      scaleMode("linear"),
      pixelsRounding(false),
      adaptGameResolutionAtRuntime(true),
      sizeOnStartupMode("adaptWidth"),
      antialiasingMode("MSAA"),
      isAntialisingEnabledOnMobile(false),
      projectUuid(""),
      useDeprecatedZeroAsDefaultZOrder(false),
      isPlayableWithKeyboard(false),
      isPlayableWithGamepad(false),
      isPlayableWithMobile(false),
      currentPlatform(nullptr),
      gdMajorVersion(gd::VersionWrapper::Major()),
      gdMinorVersion(gd::VersionWrapper::Minor()),
      gdBuildVersion(gd::VersionWrapper::Build()),
      variables(gd::VariablesContainer::SourceType::Global),
      objectsContainer(gd::ObjectsContainer::SourceType::Global),
      sceneResourcesPreloading("at-startup"),
      sceneResourcesUnloading("never") {}

Project::~Project() {}

void Project::ResetProjectUuid() { projectUuid = UUID::MakeUuid4(); }

void Project::EnsureObjectDefaultBehaviors(gd::Object& object) const {
  auto& platform = GetCurrentPlatform();
  auto& project = *this;
  auto& objectType = object.GetType();

  auto addDefaultBehavior = [&platform, &project, &object, &objectType](
                                const gd::String& behaviorType) {
    auto& behaviorMetadata =
        gd::MetadataProvider::GetBehaviorMetadata(platform, behaviorType);
    if (MetadataProvider::IsBadBehaviorMetadata(behaviorMetadata)) {
      gd::LogWarning("Object: " + objectType +
                     " has an unknown default behavior: " + behaviorType);
      return;
    }

    const gd::String& behaviorName = behaviorMetadata.GetDefaultName();

    // Check if we can keep a behavior that would have been already set up on the object.
    if (object.HasBehaviorNamed(behaviorName)) {
      const auto& behavior = object.GetBehavior(behaviorName);

      if (!behavior.IsDefaultBehavior() || behavior.GetTypeName() != behaviorType) {
        // Behavior type has changed, remove it so it is re-created.
        object.RemoveBehavior(behaviorName);
      }
    }

    if (!object.HasBehaviorNamed(behaviorName)) {
      auto* behavior = object.AddNewBehavior(
          project, behaviorType, behaviorName);
      behavior->SetDefaultBehavior(true);
    }
  };

  auto &objectMetadata =
      gd::MetadataProvider::GetObjectMetadata(platform, objectType);
  if (!MetadataProvider::IsBadObjectMetadata(objectMetadata)) {
    // Add all default behaviors.
    const auto& defaultBehaviorTypes = objectMetadata.GetDefaultBehaviors();
    for (auto &behaviorType : defaultBehaviorTypes) {
      addDefaultBehavior(behaviorType);
    }

    // Ensure there are no default behaviors that would not be required left on the object.
    for (const auto& behaviorName : object.GetAllBehaviorNames()) {
      auto& behavior = object.GetBehavior(behaviorName);
      if (!behavior.IsDefaultBehavior()) {
        // Non default behaviors are not handled by this function.
        continue;
      }

      if (defaultBehaviorTypes.find(behavior.GetTypeName()) == defaultBehaviorTypes.end()) {
        object.RemoveBehavior(behaviorName);
      }
    }
  }
  // During project deserialization, event-based object metadata are not yet
  // generated. Default behaviors will be added by
  // MetadataDeclarationHelper::UpdateCustomObjectDefaultBehaviors
  else if (!project.HasEventsBasedObject(objectType)) {
    gd::LogWarning("Object: " + name + " has an unknown type: " + objectType);
  }
}

std::unique_ptr<gd::Object> Project::CreateObject(
    const gd::String& objectType, const gd::String& name) const {
  std::unique_ptr<gd::Object> object = gd::make_unique<Object>(
      name, objectType, CreateObjectConfiguration(objectType));

  EnsureObjectDefaultBehaviors(*object);

  return std::move(object);
}

std::unique_ptr<gd::ObjectConfiguration> Project::CreateObjectConfiguration(
    const gd::String& type) const {
  if (Project::HasEventsBasedObject(type)) {
    return gd::make_unique<CustomObjectConfiguration>(*this, type);
  } else {
    // Create a base object if the type can't be found in the platform.
    return currentPlatform->CreateObjectConfiguration(type);
  }
}

bool Project::HasEventsBasedObject(const gd::String& type) const {
  const auto separatorIndex =
      type.find(PlatformExtension::GetNamespaceSeparator());
  if (separatorIndex == std::string::npos) {
    return false;
  }
  gd::String extensionName = type.substr(0, separatorIndex);
  if (!Project::HasEventsFunctionsExtensionNamed(extensionName)) {
    return false;
  }
  auto& extension = Project::GetEventsFunctionsExtension(extensionName);
  gd::String objectTypeName = type.substr(separatorIndex + 2);
  return extension.GetEventsBasedObjects().Has(objectTypeName);
}

gd::EventsBasedObject& Project::GetEventsBasedObject(const gd::String& type) {
  const auto separatorIndex =
      type.find(PlatformExtension::GetNamespaceSeparator());
  gd::String extensionName = type.substr(0, separatorIndex);
  gd::String objectTypeName = type.substr(separatorIndex + 2);

  auto& extension = Project::GetEventsFunctionsExtension(extensionName);
  return extension.GetEventsBasedObjects().Get(objectTypeName);
}

const gd::EventsBasedObject& Project::GetEventsBasedObject(
    const gd::String& type) const {
  const auto separatorIndex =
      type.find(PlatformExtension::GetNamespaceSeparator());
  gd::String extensionName = type.substr(0, separatorIndex);
  gd::String objectTypeName = type.substr(separatorIndex + 2);

  const auto& extension = Project::GetEventsFunctionsExtension(extensionName);
  return extension.GetEventsBasedObjects().Get(objectTypeName);
}

bool Project::HasEventsBasedBehavior(const gd::String& type) const {
  const auto separatorIndex =
      type.find(PlatformExtension::GetNamespaceSeparator());
  if (separatorIndex == std::string::npos) {
    return false;
  }
  gd::String extensionName = type.substr(0, separatorIndex);
  if (!Project::HasEventsFunctionsExtensionNamed(extensionName)) {
    return false;
  }
  auto& extension = Project::GetEventsFunctionsExtension(extensionName);
  gd::String behaviorTypeName = type.substr(separatorIndex + 2);
  return extension.GetEventsBasedBehaviors().Has(behaviorTypeName);
}

gd::EventsBasedBehavior& Project::GetEventsBasedBehavior(
    const gd::String& type) {
  const auto separatorIndex =
      type.find(PlatformExtension::GetNamespaceSeparator());
  gd::String extensionName = type.substr(0, separatorIndex);
  gd::String behaviorTypeName = type.substr(separatorIndex + 2);

  auto& extension = Project::GetEventsFunctionsExtension(extensionName);
  return extension.GetEventsBasedBehaviors().Get(behaviorTypeName);
}

const gd::EventsBasedBehavior& Project::GetEventsBasedBehavior(
    const gd::String& type) const {
  const auto separatorIndex =
      type.find(PlatformExtension::GetNamespaceSeparator());
  gd::String extensionName = type.substr(0, separatorIndex);
  gd::String behaviorTypeName = type.substr(separatorIndex + 2);

  auto& extension = Project::GetEventsFunctionsExtension(extensionName);
  return extension.GetEventsBasedBehaviors().Get(behaviorTypeName);
}

std::shared_ptr<gd::BaseEvent> Project::CreateEvent(
    const gd::String& type, const gd::String& platformName) {
  for (std::size_t i = 0; i < platforms.size(); ++i) {
    if (!platformName.empty() && platforms[i]->GetName() != platformName)
      continue;

    std::shared_ptr<gd::BaseEvent> event = platforms[i]->CreateEvent(type);
    if (event) return event;
  }

  return std::shared_ptr<gd::BaseEvent>();
}

Platform& Project::GetCurrentPlatform() const {
  if (currentPlatform == NULL)
    std::cout << "FATAL ERROR: Project has no assigned current platform. GD "
                 "will crash."
              << std::endl;

  return *currentPlatform;
}

void Project::AddPlatform(Platform& platform) {
  for (std::size_t i = 0; i < platforms.size(); ++i) {
    if (platforms[i] == &platform) return;
  }

  // Add the platform and make it the current one if the game has no other
  // platform.
  platforms.push_back(&platform);
  if (currentPlatform == NULL) currentPlatform = &platform;
}

void Project::SetCurrentPlatform(const gd::String& platformName) {
  for (std::size_t i = 0; i < platforms.size(); ++i) {
    if (platforms[i]->GetName() == platformName) {
      currentPlatform = platforms[i];
      return;
    }
  }
}

bool Project::RemovePlatform(const gd::String& platformName) {
  if (platforms.size() <= 1) return false;

  for (std::size_t i = 0; i < platforms.size(); ++i) {
    if (platforms[i]->GetName() == platformName) {
      // Remove the platform, ensuring that currentPlatform remains correct.
      if (currentPlatform == platforms[i]) currentPlatform = platforms.back();
      if (currentPlatform == platforms[i]) currentPlatform = platforms[0];
      platforms.erase(platforms.begin() + i);

      return true;
    }
  }

  return false;
}

bool Project::HasLayoutNamed(const gd::String& name) const {
  return (find_if(scenes.begin(),
                  scenes.end(),
                  [&name](const std::unique_ptr<gd::Layout>& layout) {
                    return layout->GetName() == name;
                  }) != scenes.end());
}
gd::Layout& Project::GetLayout(const gd::String& name) {
  return *(*find_if(
      scenes.begin(), scenes.end(), [&name](const std::unique_ptr<gd::Layout>& layout) {
        return layout->GetName() == name;
      }));
}
const gd::Layout& Project::GetLayout(const gd::String& name) const {
  return *(*find_if(
      scenes.begin(), scenes.end(), [&name](const std::unique_ptr<gd::Layout>& layout) {
        return layout->GetName() == name;
      }));
}
gd::Layout& Project::GetLayout(std::size_t index) { return *scenes[index]; }
const gd::Layout& Project::GetLayout(std::size_t index) const {
  return *scenes[index];
}
std::size_t Project::GetLayoutPosition(const gd::String& name) const {
  for (std::size_t i = 0; i < scenes.size(); ++i) {
    if (scenes[i]->GetName() == name) return i;
  }
  return gd::String::npos;
}
std::size_t Project::GetLayoutsCount() const { return scenes.size(); }

void Project::SwapLayouts(std::size_t first, std::size_t second) {
  if (first >= scenes.size() || second >= scenes.size()) return;

  std::iter_swap(scenes.begin() + first, scenes.begin() + second);
}

gd::Layout& Project::InsertNewLayout(const gd::String& name,
                                     std::size_t position) {
  gd::Layout& newlyInsertedLayout = *(*(scenes.emplace(
      position < scenes.size() ? scenes.begin() + position : scenes.end(),
      new Layout())));

  newlyInsertedLayout.SetName(name);
  newlyInsertedLayout.UpdateBehaviorsSharedData(*this);

  return newlyInsertedLayout;
}

gd::Layout& Project::InsertLayout(const gd::Layout& layout,
                                  std::size_t position) {
  gd::Layout& newlyInsertedLayout = *(*(scenes.emplace(
      position < scenes.size() ? scenes.begin() + position : scenes.end(),
      new Layout(layout))));

  newlyInsertedLayout.UpdateBehaviorsSharedData(*this);

  return newlyInsertedLayout;
}

void Project::RemoveLayout(const gd::String& name) {
  std::vector<std::unique_ptr<gd::Layout> >::iterator scene =
      find_if(scenes.begin(), scenes.end(), [&name](const std::unique_ptr<gd::Layout>& layout) {
        return layout->GetName() == name;
      });
  if (scene == scenes.end()) return;

  scenes.erase(scene);
}

bool Project::HasExternalEventsNamed(const gd::String& name) const {
  return (find_if(externalEvents.begin(),
                  externalEvents.end(),
                  [&name](const std::unique_ptr<gd::ExternalEvents>& externalEvents) {
                    return externalEvents->GetName() == name;
                  }) != externalEvents.end());
}
gd::ExternalEvents& Project::GetExternalEvents(const gd::String& name) {
  return *(*find_if(externalEvents.begin(),
                    externalEvents.end(),
                    [&name](const std::unique_ptr<gd::ExternalEvents>& externalEvents) {
                      return externalEvents->GetName() == name;
                    }));
}
const gd::ExternalEvents& Project::GetExternalEvents(
    const gd::String& name) const {
  return *(*find_if(externalEvents.begin(),
                    externalEvents.end(),
                    [&name](const std::unique_ptr<gd::ExternalEvents>& externalEvents) {
                      return externalEvents->GetName() == name;
                    }));
}
gd::ExternalEvents& Project::GetExternalEvents(std::size_t index) {
  return *externalEvents[index];
}
const gd::ExternalEvents& Project::GetExternalEvents(std::size_t index) const {
  return *externalEvents[index];
}
std::size_t Project::GetExternalEventsPosition(const gd::String& name) const {
  for (std::size_t i = 0; i < externalEvents.size(); ++i) {
    if (externalEvents[i]->GetName() == name) return i;
  }
  return gd::String::npos;
}
std::size_t Project::GetExternalEventsCount() const {
  return externalEvents.size();
}

gd::ExternalEvents& Project::InsertNewExternalEvents(const gd::String& name,
                                                     std::size_t position) {
  gd::ExternalEvents& newlyInsertedExternalEvents = *(*(externalEvents.emplace(
      position < externalEvents.size() ? externalEvents.begin() + position
                                       : externalEvents.end(),
      new gd::ExternalEvents())));

  newlyInsertedExternalEvents.SetName(name);

  return newlyInsertedExternalEvents;
}

gd::ExternalEvents& Project::InsertExternalEvents(
    const gd::ExternalEvents& events, std::size_t position) {
  gd::ExternalEvents& newlyInsertedExternalEvents = *(*(externalEvents.emplace(
      position < externalEvents.size() ? externalEvents.begin() + position
                                       : externalEvents.end(),
      new gd::ExternalEvents(events))));

  return newlyInsertedExternalEvents;
}

void Project::RemoveExternalEvents(const gd::String& name) {
  std::vector<std::unique_ptr<gd::ExternalEvents> >::iterator events =
      find_if(externalEvents.begin(),
              externalEvents.end(),
              [&name](const std::unique_ptr<gd::ExternalEvents>& externalEvents) {
                return externalEvents->GetName() == name;
              });
  if (events == externalEvents.end()) return;

  externalEvents.erase(events);
}

void Project::MoveLayout(std::size_t oldIndex, std::size_t newIndex) {
  if (oldIndex >= scenes.size() || newIndex >= scenes.size()) return;

  std::unique_ptr<gd::Layout> scene = std::move(scenes[oldIndex]);
  scenes.erase(scenes.begin() + oldIndex);
  scenes.insert(scenes.begin() + newIndex, std::move(scene));
};

void Project::MoveExternalEvents(std::size_t oldIndex, std::size_t newIndex) {
  if (oldIndex >= externalEvents.size() || newIndex >= externalEvents.size())
    return;

  std::unique_ptr<gd::ExternalEvents> externalEventsItem =
      std::move(externalEvents[oldIndex]);
  externalEvents.erase(externalEvents.begin() + oldIndex);
  externalEvents.insert(externalEvents.begin() + newIndex,
                        std::move(externalEventsItem));
};

void Project::MoveExternalLayout(std::size_t oldIndex, std::size_t newIndex) {
  if (oldIndex >= externalLayouts.size() || newIndex >= externalLayouts.size())
    return;

  std::unique_ptr<gd::ExternalLayout> externalLayout =
      std::move(externalLayouts[oldIndex]);
  externalLayouts.erase(externalLayouts.begin() + oldIndex);
  externalLayouts.insert(externalLayouts.begin() + newIndex,
                         std::move(externalLayout));
};

void Project::MoveEventsFunctionsExtension(std::size_t oldIndex,
                                           std::size_t newIndex) {
  if (oldIndex >= eventsFunctionsExtensions.size() ||
      newIndex >= eventsFunctionsExtensions.size())
    return;

  std::unique_ptr<gd::EventsFunctionsExtension> eventsFunctionsExtension =
      std::move(eventsFunctionsExtensions[oldIndex]);
  eventsFunctionsExtensions.erase(eventsFunctionsExtensions.begin() + oldIndex);
  eventsFunctionsExtensions.insert(eventsFunctionsExtensions.begin() + newIndex,
                                   std::move(eventsFunctionsExtension));
};

void Project::SwapExternalEvents(std::size_t first, std::size_t second) {
  if (first >= externalEvents.size() || second >= externalEvents.size()) return;

  std::iter_swap(externalEvents.begin() + first,
                 externalEvents.begin() + second);
}

void Project::SwapExternalLayouts(std::size_t first, std::size_t second) {
  if (first >= externalLayouts.size() || second >= externalLayouts.size())
    return;

  std::iter_swap(externalLayouts.begin() + first,
                 externalLayouts.begin() + second);
}
bool Project::HasExternalLayoutNamed(const gd::String& name) const {
  return (find_if(externalLayouts.begin(),
                  externalLayouts.end(),
                  [&name](const std::unique_ptr<gd::ExternalLayout>& externalLayout) {
                    return externalLayout->GetName() == name;
                  }) != externalLayouts.end());
}
gd::ExternalLayout& Project::GetExternalLayout(const gd::String& name) {
  return *(*find_if(externalLayouts.begin(),
                    externalLayouts.end(),
                    [&name](const std::unique_ptr<gd::ExternalLayout>& externalLayout) {
                      return externalLayout->GetName() == name;
                    }));
}
const gd::ExternalLayout& Project::GetExternalLayout(
    const gd::String& name) const {
  return *(*find_if(externalLayouts.begin(),
                    externalLayouts.end(),
                    [&name](const std::unique_ptr<gd::ExternalLayout>& externalLayout) {
                      return externalLayout->GetName() == name;
                    }));
}
gd::ExternalLayout& Project::GetExternalLayout(std::size_t index) {
  return *externalLayouts[index];
}
const gd::ExternalLayout& Project::GetExternalLayout(std::size_t index) const {
  return *externalLayouts[index];
}
std::size_t Project::GetExternalLayoutPosition(const gd::String& name) const {
  for (std::size_t i = 0; i < externalLayouts.size(); ++i) {
    if (externalLayouts[i]->GetName() == name) return i;
  }
  return gd::String::npos;
}

std::size_t Project::GetExternalLayoutsCount() const {
  return externalLayouts.size();
}

gd::ExternalLayout& Project::InsertNewExternalLayout(const gd::String& name,
                                                     std::size_t position) {
  gd::ExternalLayout& newlyInsertedExternalLayout = *(*(externalLayouts.emplace(
      position < externalLayouts.size() ? externalLayouts.begin() + position
                                        : externalLayouts.end(),
      new gd::ExternalLayout())));

  newlyInsertedExternalLayout.SetName(name);
  return newlyInsertedExternalLayout;
}

gd::ExternalLayout& Project::InsertExternalLayout(
    const gd::ExternalLayout& layout, std::size_t position) {
  gd::ExternalLayout& newlyInsertedExternalLayout = *(*(externalLayouts.emplace(
      position < externalLayouts.size() ? externalLayouts.begin() + position
                                        : externalLayouts.end(),
      new gd::ExternalLayout(layout))));

  return newlyInsertedExternalLayout;
}

void Project::RemoveExternalLayout(const gd::String& name) {
  std::vector<std::unique_ptr<gd::ExternalLayout> >::iterator externalLayout =
      find_if(externalLayouts.begin(),
              externalLayouts.end(),
              [&name](const std::unique_ptr<gd::ExternalLayout>& externalLayout) {
                return externalLayout->GetName() == name;
              });
  if (externalLayout == externalLayouts.end()) return;

  externalLayouts.erase(externalLayout);
}

void Project::SwapEventsFunctionsExtensions(std::size_t first,
                                            std::size_t second) {
  if (first >= eventsFunctionsExtensions.size() ||
      second >= eventsFunctionsExtensions.size())
    return;

  std::iter_swap(eventsFunctionsExtensions.begin() + first,
                 eventsFunctionsExtensions.begin() + second);
}
bool Project::HasEventsFunctionsExtensionNamed(const gd::String& name) const {
  return (
      find_if(
          eventsFunctionsExtensions.begin(),
          eventsFunctionsExtensions.end(),
          [&name](
              const std::unique_ptr<gd::EventsFunctionsExtension>& extension) {
            return extension->GetName() == name;
          }) != eventsFunctionsExtensions.end());
}
gd::EventsFunctionsExtension& Project::GetEventsFunctionsExtension(
    const gd::String& name) {
  return *(*find_if(
      eventsFunctionsExtensions.begin(),
      eventsFunctionsExtensions.end(),
      [&name](const std::unique_ptr<gd::EventsFunctionsExtension>& extension) {
        return extension->GetName() == name;
      }));
}
const gd::EventsFunctionsExtension& Project::GetEventsFunctionsExtension(
    const gd::String& name) const {
  return *(*find_if(
      eventsFunctionsExtensions.begin(),
      eventsFunctionsExtensions.end(),
      [&name](const std::unique_ptr<gd::EventsFunctionsExtension>& extension) {
        return extension->GetName() == name;
      }));
}
gd::EventsFunctionsExtension& Project::GetEventsFunctionsExtension(
    std::size_t index) {
  return *eventsFunctionsExtensions[index];
}
const gd::EventsFunctionsExtension& Project::GetEventsFunctionsExtension(
    std::size_t index) const {
  return *eventsFunctionsExtensions[index];
}
std::size_t Project::GetEventsFunctionsExtensionPosition(
    const gd::String& name) const {
  for (std::size_t i = 0; i < eventsFunctionsExtensions.size(); ++i) {
    if (eventsFunctionsExtensions[i]->GetName() == name) return i;
  }
  return gd::String::npos;
}

std::size_t Project::GetEventsFunctionsExtensionsCount() const {
  return eventsFunctionsExtensions.size();
}

gd::EventsFunctionsExtension& Project::InsertNewEventsFunctionsExtension(
    const gd::String& name, std::size_t position) {
  gd::EventsFunctionsExtension& newlyInsertedEventsFunctionsExtension =
      *(*(eventsFunctionsExtensions.emplace(
          position < eventsFunctionsExtensions.size()
              ? eventsFunctionsExtensions.begin() + position
              : eventsFunctionsExtensions.end(),
          new gd::EventsFunctionsExtension())));

  newlyInsertedEventsFunctionsExtension.SetName(name);
  return newlyInsertedEventsFunctionsExtension;
}

gd::EventsFunctionsExtension& Project::InsertEventsFunctionsExtension(
    const gd::EventsFunctionsExtension& extension, std::size_t position) {
  gd::EventsFunctionsExtension& newlyInsertedEventsFunctionsExtension =
      *(*(eventsFunctionsExtensions.emplace(
          position < eventsFunctionsExtensions.size()
              ? eventsFunctionsExtensions.begin() + position
              : eventsFunctionsExtensions.end(),
          new gd::EventsFunctionsExtension(extension))));

  return newlyInsertedEventsFunctionsExtension;
}

void Project::RemoveEventsFunctionsExtension(const gd::String& name) {
  std::vector<std::unique_ptr<gd::EventsFunctionsExtension> >::iterator
      eventsFunctionExtension = find_if(
          eventsFunctionsExtensions.begin(),
          eventsFunctionsExtensions.end(),
          [&name](
              const std::unique_ptr<gd::EventsFunctionsExtension>& extension) {
            return extension->GetName() == name;
          });
  if (eventsFunctionExtension == eventsFunctionsExtensions.end()) return;

  eventsFunctionsExtensions.erase(eventsFunctionExtension);
}
void Project::ClearEventsFunctionsExtensions() {
  eventsFunctionsExtensions.clear();
}

void Project::UnserializeFrom(const SerializerElement& element) {
  const SerializerElement& gdVersionElement =
      element.GetChild("gdVersion", 0, "GDVersion");
  gdMajorVersion =
      gdVersionElement.GetIntAttribute("major", gdMajorVersion, "Major");
  gdMinorVersion =
      gdVersionElement.GetIntAttribute("minor", gdMinorVersion, "Minor");
  gdBuildVersion = gdVersionElement.GetIntAttribute("build", 0, "Build");
  int revision = gdVersionElement.GetIntAttribute("revision", 0, "Revision");

  if (gdMajorVersion > gd::VersionWrapper::Major())
    gd::LogWarning(
        "The version of GDevelop used to create this game seems to be a new "
        "version.\nGDevelop may fail to open the game, or data may be "
        "missing.\nYou should check if a new version of GDevelop is "
        "available.");
  else {
    if ((gdMajorVersion == gd::VersionWrapper::Major() &&
         gdMinorVersion > gd::VersionWrapper::Minor()) ||
        (gdMajorVersion == gd::VersionWrapper::Major() &&
         gdMinorVersion == gd::VersionWrapper::Minor() &&
         gdBuildVersion > gd::VersionWrapper::Build()) ||
        (gdMajorVersion == gd::VersionWrapper::Major() &&
         gdMinorVersion == gd::VersionWrapper::Minor() &&
         gdBuildVersion == gd::VersionWrapper::Build() &&
         revision > gd::VersionWrapper::Revision())) {
      gd::LogWarning(
          "The version of GDevelop used to create this game seems to be "
          "greater.\nGDevelop may fail to open the game, or data may be "
          "missing.\nYou should check if a new version of GDevelop is "
          "available.");
    }
  }

  const SerializerElement& propElement =
      element.GetChild("properties", 0, "Info");
  SetName(propElement.GetChild("name", 0, "Nom").GetValue().GetString());
  SetDescription(propElement.GetChild("description", 0).GetValue().GetString());
  SetVersion(propElement.GetStringAttribute("version", "1.0.0"));
  SetGameResolutionSize(
      propElement.GetChild("windowWidth", 0, "WindowW").GetValue().GetInt(),
      propElement.GetChild("windowHeight", 0, "WindowH").GetValue().GetInt());
  SetMaximumFPS(
      propElement.GetChild("maxFPS", 0, "FPSmax").GetValue().GetInt());
  SetMinimumFPS(
      propElement.GetChild("minFPS", 0, "FPSmin").GetValue().GetInt());
  SetVerticalSyncActivatedByDefault(
      propElement.GetChild("verticalSync").GetValue().GetBool());
  SetScaleMode(propElement.GetStringAttribute("scaleMode", "linear"));
  SetPixelsRounding(propElement.GetBoolAttribute("pixelsRounding", false));
  SetAdaptGameResolutionAtRuntime(
      propElement.GetBoolAttribute("adaptGameResolutionAtRuntime", false));
  SetSizeOnStartupMode(propElement.GetStringAttribute("sizeOnStartupMode", ""));
  SetAntialiasingMode(
      propElement.GetStringAttribute("antialiasingMode", "MSAA"));
  SetAntialisingEnabledOnMobile(
      propElement.GetBoolAttribute("antialisingEnabledOnMobile", false));
  SetProjectUuid(propElement.GetStringAttribute("projectUuid", ""));
  SetAuthor(propElement.GetChild("author", 0, "Auteur").GetValue().GetString());
  SetPackageName(propElement.GetStringAttribute("packageName"));
  SetTemplateSlug(propElement.GetStringAttribute("templateSlug"));
  SetOrientation(propElement.GetStringAttribute("orientation", "default"));
  SetFolderProject(propElement.GetBoolAttribute("folderProject"));
  SetLastCompilationDirectory(propElement
                                  .GetChild("latestCompilationDirectory",
                                            0,
                                            "LatestCompilationDirectory")
                                  .GetValue()
                                  .GetString());
  platformSpecificAssets.UnserializeFrom(
      propElement.GetChild("platformSpecificAssets"));
  loadingScreen.UnserializeFrom(propElement.GetChild("loadingScreen"));
  watermark.UnserializeFrom(propElement.GetChild("watermark"));

  authorIds.clear();
  auto& authorIdsElement = propElement.GetChild("authorIds");
  authorIdsElement.ConsiderAsArray();
  for (std::size_t i = 0; i < authorIdsElement.GetChildrenCount(); ++i) {
    authorIds.push_back(authorIdsElement.GetChild(i).GetStringValue());
  }
  authorUsernames.clear();
  auto& authorUsernamesElement = propElement.GetChild("authorUsernames");
  authorUsernamesElement.ConsiderAsArray();
  for (std::size_t i = 0; i < authorUsernamesElement.GetChildrenCount(); ++i) {
    authorUsernames.push_back(
        authorUsernamesElement.GetChild(i).GetStringValue());
  }

  categories.clear();
  auto& categoriesElement = propElement.GetChild("categories");
  categoriesElement.ConsiderAsArray();
  for (std::size_t i = 0; i < categoriesElement.GetChildrenCount(); ++i) {
    categories.push_back(categoriesElement.GetChild(i).GetStringValue());
  }

  auto& playableDevicesElement = propElement.GetChild("playableDevices");
  playableDevicesElement.ConsiderAsArray();
  for (std::size_t i = 0; i < playableDevicesElement.GetChildrenCount(); ++i) {
    const auto& playableDevice =
        playableDevicesElement.GetChild(i).GetStringValue();
    if (playableDevice == "keyboard") {
      isPlayableWithKeyboard = true;
    } else if (playableDevice == "gamepad") {
      isPlayableWithGamepad = true;
    } else if (playableDevice == "mobile") {
      isPlayableWithMobile = true;
    }
  }

  // Compatibility with GD <= 5.0.0-beta101
  if (VersionWrapper::IsOlderOrEqual(
          gdMajorVersion, gdMinorVersion, gdBuildVersion, 0, 4, 0, 98, 0) &&
      !propElement.HasAttribute("useDeprecatedZeroAsDefaultZOrder")) {
    useDeprecatedZeroAsDefaultZOrder = true;
  } else {
    useDeprecatedZeroAsDefaultZOrder =
        propElement.GetBoolAttribute("useDeprecatedZeroAsDefaultZOrder", false);
  }
  // end of compatibility code

  // Compatibility with GD <= 5.0.0-beta101
  if (!propElement.HasAttribute("projectUuid") &&
      !propElement.HasChild("projectUuid")) {
    ResetProjectUuid();
  }
  // end of compatibility code

  extensionProperties.UnserializeFrom(
      propElement.GetChild("extensionProperties"));

  // Compatibility with GD <= 5.0.0-beta98
  // Move AdMob App ID from project property to extension property.
  if (propElement.GetStringAttribute("adMobAppId", "") != "") {
    extensionProperties.SetValue(
        "AdMob",
        "AdMobAppId",
        propElement.GetStringAttribute("adMobAppId", ""));
  }
  // end of compatibility code

  currentPlatform = NULL;
  gd::String currentPlatformName =
      propElement.GetChild("currentPlatform").GetValue().GetString();
  // Compatibility code
  if (VersionWrapper::IsOlderOrEqual(
          gdMajorVersion, gdMajorVersion, gdMinorVersion, 0, 3, 4, 73, 0)) {
    if (currentPlatformName == "Game Develop C++ platform")
      currentPlatformName = "GDevelop C++ platform";
    if (currentPlatformName == "Game Develop JS platform")
      currentPlatformName = "GDevelop JS platform";
  }
  // End of Compatibility code

  const SerializerElement& platformsElement =
      propElement.GetChild("platforms", 0, "Platforms");
  platformsElement.ConsiderAsArrayOf("platform", "Platform");
  for (std::size_t i = 0; i < platformsElement.GetChildrenCount(); ++i) {
    gd::String name = platformsElement.GetChild(i).GetStringAttribute("name");
    // Compatibility code
    if (VersionWrapper::IsOlderOrEqual(
            gdMajorVersion, gdMajorVersion, gdMinorVersion, 0, 3, 4, 73, 0)) {
      if (name == "Game Develop C++ platform") name = "GDevelop C++ platform";
      if (name == "Game Develop JS platform") name = "GDevelop JS platform";
    }
    // End of Compatibility code

    gd::Platform* platform = gd::PlatformManager::Get()->GetPlatform(name);

    if (platform) {
      AddPlatform(*platform);
      if (platform->GetName() == currentPlatformName ||
          currentPlatformName.empty())
        currentPlatform = platform;
    } else {
      std::cout << "Platform \"" << name << "\" is unknown." << std::endl;
    }
  }

  // Compatibility code
  if (platformsElement.GetChildrenCount() == 0) {
    // Compatibility with GD2.x
    platforms.push_back(
        gd::PlatformManager::Get()->GetPlatform("GDevelop C++ platform"));
    currentPlatform = platforms.back();
  }
  // End of Compatibility code

  if (currentPlatform == NULL && !platforms.empty())
    currentPlatform = platforms.back();

  eventsFunctionsExtensions.clear();
  const SerializerElement& eventsFunctionsExtensionsElement =
      element.GetChild("eventsFunctionsExtensions");
  UnserializeAndInsertExtensionsFrom(eventsFunctionsExtensionsElement);

  objectsContainer.GetObjectGroups().UnserializeFrom(
      element.GetChild("objectsGroups", 0, "ObjectGroups"));
  resourcesManager.UnserializeFrom(
      element.GetChild("resources", 0, "Resources"));
  objectsContainer.UnserializeObjectsFrom(*this, element.GetChild("objects", 0, "Objects"));
  if (element.HasChild("objectsFolderStructure")) {
    objectsContainer.UnserializeFoldersFrom(*this, element.GetChild("objectsFolderStructure", 0));
  }
  objectsContainer.AddMissingObjectsInRootFolder();

  GetVariables().UnserializeFrom(element.GetChild("variables", 0, "Variables"));

  scenes.clear();
  const SerializerElement& layoutsElement =
      element.GetChild("layouts", 0, "Scenes");
  layoutsElement.ConsiderAsArrayOf("layout", "Scene");
  for (std::size_t i = 0; i < layoutsElement.GetChildrenCount(); ++i) {
    const SerializerElement& layoutElement = layoutsElement.GetChild(i);

    gd::Layout& layout = InsertNewLayout(
        layoutElement.GetStringAttribute("name", "", "nom"), -1);
    layout.UnserializeFrom(*this, layoutElement);
  }
  SetFirstLayout(element.GetChild("firstLayout").GetStringValue());

  externalEvents.clear();
  const SerializerElement& externalEventsElement =
      element.GetChild("externalEvents", 0, "ExternalEvents");
  externalEventsElement.ConsiderAsArrayOf("externalEvents", "ExternalEvents");
  for (std::size_t i = 0; i < externalEventsElement.GetChildrenCount(); ++i) {
    const SerializerElement& externalEventElement =
        externalEventsElement.GetChild(i);

    gd::ExternalEvents& externalEvents = InsertNewExternalEvents(
        externalEventElement.GetStringAttribute("name", "", "Name"),
        GetExternalEventsCount());
    externalEvents.UnserializeFrom(*this, externalEventElement);
  }

  externalLayouts.clear();
  const SerializerElement& externalLayoutsElement =
      element.GetChild("externalLayouts", 0, "ExternalLayouts");
  externalLayoutsElement.ConsiderAsArrayOf("externalLayout", "ExternalLayout");
  for (std::size_t i = 0; i < externalLayoutsElement.GetChildrenCount(); ++i) {
    const SerializerElement& externalLayoutElement =
        externalLayoutsElement.GetChild(i);

    gd::ExternalLayout& newExternalLayout =
        InsertNewExternalLayout("", GetExternalLayoutsCount());
    newExternalLayout.UnserializeFrom(externalLayoutElement);
  }
}

void Project::UnserializeAndInsertExtensionsFrom(
  const gd::SerializerElement &eventsFunctionsExtensionsElement) {
  eventsFunctionsExtensionsElement.ConsiderAsArrayOf(
      "eventsFunctionsExtension");

  std::map<gd::String, size_t> extensionNameToElementIndex;
  std::map<gd::String, gd::SerializerElement> objectTypeToVariantsElement;

  // First, only unserialize behaviors and objects names.
  // As event based objects can contains custom behaviors and custom objects,
  // this allows them to reference EventBasedBehavior and EventBasedObject
  // respectively.
  for (std::size_t i = 0;
       i < eventsFunctionsExtensionsElement.GetChildrenCount();
       ++i) {
    const SerializerElement& eventsFunctionsExtensionElement =
        eventsFunctionsExtensionsElement.GetChild(i);
    const gd::String& name = eventsFunctionsExtensionElement.GetStringAttribute("name");
    extensionNameToElementIndex[name] = i;

    gd::EventsFunctionsExtension& eventsFunctionsExtension =
        HasEventsFunctionsExtensionNamed(name)
            ? GetEventsFunctionsExtension(name)
            : InsertNewEventsFunctionsExtension(
                  name, GetEventsFunctionsExtensionsCount());

    // Backup the events-based object variants
    for (auto &eventsBasedObject :
         eventsFunctionsExtension.GetEventsBasedObjects().GetInternalVector()) {
      gd::SerializerElement variantsElement;
      eventsBasedObject->GetVariants().SerializeVariantsTo(variantsElement);
      objectTypeToVariantsElement[gd::PlatformExtension::GetObjectFullType(
          name, eventsBasedObject->GetName())] = variantsElement;
    }

    eventsFunctionsExtension.UnserializeExtensionDeclarationFrom(
        *this, eventsFunctionsExtensionElement);
  }

  // Then unserialize functions, behaviors and objects content.
  for (gd::String &extensionName :
       GetUnserializingOrderExtensionNames(eventsFunctionsExtensionsElement)) {

    size_t extensionIndex = GetEventsFunctionsExtensionPosition(extensionName);
    if (extensionIndex == gd::String::npos) {
      // Should never happen because the extension was added in the first pass.
      gd::LogError("Can't find extension " + extensionName + " in the list of extensions in second pass of unserialization.");
      continue;
    }
    auto& partiallyLoadedExtension = eventsFunctionsExtensions.at(extensionIndex);

    if (extensionNameToElementIndex.find(extensionName) == extensionNameToElementIndex.end()) {
      // Should never happen because the extension element is present.
      gd::LogError("Can't find extension element to unserialize for " + extensionName + " in second pass of unserialization.");
      continue;
    }
    size_t elementIndex = extensionNameToElementIndex[extensionName];
    const SerializerElement &eventsFunctionsExtensionElement =
        eventsFunctionsExtensionsElement.GetChild(elementIndex);

    partiallyLoadedExtension
        ->UnserializeExtensionImplementationFrom(
            *this, eventsFunctionsExtensionElement);

    for (auto &pair : objectTypeToVariantsElement) {
      auto &objectType = pair.first;
      auto &variantsElement = pair.second;

      auto &eventsBasedObject = GetEventsBasedObject(objectType);
      eventsBasedObject.GetVariants().UnserializeVariantsFrom(*this,
                                                              variantsElement);
    }
  }
}

std::vector<gd::String> Project::GetUnserializingOrderExtensionNames(
    const gd::SerializerElement &eventsFunctionsExtensionsElement) {
  eventsFunctionsExtensionsElement.ConsiderAsArrayOf(
      "eventsFunctionsExtension");

  // Some extension have custom objects, which have child objects coming from other extension.
  // These child objects must be loaded completely before the parent custom obejct can be unserialized.
  // This implies: an order on the extension unserialization (and no cycles).

  // At the beginning, everything is yet to be loaded.
  std::map<gd::String, size_t> extensionNameToElementIndex;
  std::vector<gd::String> remainingExtensionNames(
      eventsFunctionsExtensionsElement.GetChildrenCount());
  for (std::size_t i = 0; i < eventsFunctionsExtensionsElement.GetChildrenCount(); ++i) {
    const SerializerElement& eventsFunctionsExtensionElement =
        eventsFunctionsExtensionsElement.GetChild(i);
    const gd::String& name = eventsFunctionsExtensionElement.GetStringAttribute("name");

    remainingExtensionNames[i] = name;
    extensionNameToElementIndex[name] = i;
  }

  // Helper allowing to find if an extension has an object that depends on
  // at least one other object from another extension that is not loaded yet.
  auto isDependentFromRemainingExtensions =
      [&remainingExtensionNames](
          const gd::SerializerElement &eventsFunctionsExtensionElement) {
        auto &eventsBasedObjectsElement =
            eventsFunctionsExtensionElement.GetChild("eventsBasedObjects");
        eventsBasedObjectsElement.ConsiderAsArrayOf("eventsBasedObject");
        for (std::size_t eventsBasedObjectsIndex = 0;
             eventsBasedObjectsIndex <
             eventsBasedObjectsElement.GetChildrenCount();
             ++eventsBasedObjectsIndex) {
          auto &objectsElement =
              eventsBasedObjectsElement.GetChild(eventsBasedObjectsIndex)
                  .GetChild("objects");
          objectsElement.ConsiderAsArrayOf("object");

          for (std::size_t objectIndex = 0;
               objectIndex < objectsElement.GetChildrenCount(); ++objectIndex) {
            const gd::String &objectType =
                objectsElement.GetChild(objectIndex).GetStringAttribute("type");

            gd::String extensionName =
                eventsFunctionsExtensionElement.GetStringAttribute("name");
            gd::String usedExtensionName =
                gd::PlatformExtension::GetExtensionFromFullObjectType(objectType);

            if (usedExtensionName != extensionName &&
                std::find(remainingExtensionNames.begin(),
                          remainingExtensionNames.end(),
                          usedExtensionName) != remainingExtensionNames.end()) {
              return true;
            }
          }
        }
        return false;
      };

  // Find the order of loading so that the extensions are loaded when all the other
  // extensions they depend on are already loaded.
  std::vector<gd::String> loadOrderExtensionNames;
  bool foundAnyExtension = true;
  while (foundAnyExtension) {
    foundAnyExtension = false;
    for (std::size_t i = 0; i < remainingExtensionNames.size(); ++i) {
      auto extensionName = remainingExtensionNames[i];

      size_t elementIndex = extensionNameToElementIndex[extensionName];
      const SerializerElement &eventsFunctionsExtensionElement =
          eventsFunctionsExtensionsElement.GetChild(elementIndex);

      if (!isDependentFromRemainingExtensions(
              eventsFunctionsExtensionElement)) {
        loadOrderExtensionNames.push_back(extensionName);
        remainingExtensionNames.erase(remainingExtensionNames.begin() + i);
        i--;
        foundAnyExtension = true;
      }
    }
  }
  return loadOrderExtensionNames;
}

void Project::SerializeTo(SerializerElement& element) const {
  SerializerElement& versionElement = element.AddChild("gdVersion");
  versionElement.SetAttribute("major", gd::VersionWrapper::Major());
  versionElement.SetAttribute("minor", gd::VersionWrapper::Minor());
  versionElement.SetAttribute("build", gd::VersionWrapper::Build());
  versionElement.SetAttribute("revision", gd::VersionWrapper::Revision());

  SerializerElement& propElement = element.AddChild("properties");
  propElement.AddChild("name").SetValue(GetName());
  propElement.AddChild("description").SetValue(GetDescription());
  propElement.SetAttribute("version", GetVersion());
  propElement.AddChild("author").SetValue(GetAuthor());
  propElement.AddChild("windowWidth").SetValue(GetGameResolutionWidth());
  propElement.AddChild("windowHeight").SetValue(GetGameResolutionHeight());
  propElement.AddChild("latestCompilationDirectory")
      .SetValue(GetLastCompilationDirectory());
  propElement.AddChild("maxFPS").SetValue(GetMaximumFPS());
  propElement.AddChild("minFPS").SetValue(GetMinimumFPS());
  propElement.AddChild("verticalSync")
      .SetValue(IsVerticalSynchronizationEnabledByDefault());
  propElement.SetAttribute("scaleMode", scaleMode);
  propElement.SetAttribute("pixelsRounding", pixelsRounding);
  propElement.SetAttribute("adaptGameResolutionAtRuntime",
                           adaptGameResolutionAtRuntime);
  propElement.SetAttribute("sizeOnStartupMode", sizeOnStartupMode);
  propElement.SetAttribute("antialiasingMode", antialiasingMode);
  propElement.SetAttribute("antialisingEnabledOnMobile",
                           isAntialisingEnabledOnMobile);
  propElement.SetAttribute("projectUuid", projectUuid);
  propElement.SetAttribute("folderProject", folderProject);
  propElement.SetAttribute("packageName", packageName);
  propElement.SetAttribute("templateSlug", templateSlug);
  propElement.SetAttribute("orientation", orientation);
  platformSpecificAssets.SerializeTo(
      propElement.AddChild("platformSpecificAssets"));
  loadingScreen.SerializeTo(propElement.AddChild("loadingScreen"));
  watermark.SerializeTo(propElement.AddChild("watermark"));

  auto& authorIdsElement = propElement.AddChild("authorIds");
  authorIdsElement.ConsiderAsArray();
  for (const auto& authorId : authorIds) {
    authorIdsElement.AddChild("").SetStringValue(authorId);
  }
  auto& authorUsernamesElement = propElement.AddChild("authorUsernames");
  authorUsernamesElement.ConsiderAsArray();
  for (const auto& authorUsername : authorUsernames) {
    authorUsernamesElement.AddChild("").SetStringValue(authorUsername);
  }

  auto& categoriesElement = propElement.AddChild("categories");
  categoriesElement.ConsiderAsArray();
  for (const auto& category : categories) {
    categoriesElement.AddChild("").SetStringValue(category);
  }

  auto& playableDevicesElement = propElement.AddChild("playableDevices");
  playableDevicesElement.ConsiderAsArray();
  if (isPlayableWithKeyboard) {
    playableDevicesElement.AddChild("").SetStringValue("keyboard");
  }
  if (isPlayableWithGamepad) {
    playableDevicesElement.AddChild("").SetStringValue("gamepad");
  }
  if (isPlayableWithMobile) {
    playableDevicesElement.AddChild("").SetStringValue("mobile");
  }

  // Compatibility with GD <= 5.0.0-beta101
  if (useDeprecatedZeroAsDefaultZOrder) {
    propElement.SetAttribute("useDeprecatedZeroAsDefaultZOrder", true);
  }
  // end of compatibility code

  extensionProperties.SerializeTo(propElement.AddChild("extensionProperties"));

  SerializerElement& platformsElement = propElement.AddChild("platforms");
  platformsElement.ConsiderAsArrayOf("platform");
  for (std::size_t i = 0; i < platforms.size(); ++i) {
    if (platforms[i] == NULL) {
      std::cout << "ERROR: The project has a platform which is NULL.";
      continue;
    }

    platformsElement.AddChild("platform")
        .SetAttribute("name", platforms[i]->GetName());
  }
  if (currentPlatform != NULL)
    propElement.AddChild("currentPlatform")
        .SetValue(currentPlatform->GetName());
  else
    std::cout << "ERROR: The project current platform is NULL.";

  if (sceneResourcesPreloading != "at-startup") {
    propElement.SetAttribute("sceneResourcesPreloading", sceneResourcesPreloading);
  }
  if (sceneResourcesUnloading != "never") {
    propElement.SetAttribute("sceneResourcesUnloading", sceneResourcesUnloading);
  }

  resourcesManager.SerializeTo(element.AddChild("resources"));
  objectsContainer.SerializeObjectsTo(element.AddChild("objects"));
  objectsContainer.SerializeFoldersTo(element.AddChild("objectsFolderStructure"));
  objectsContainer.GetObjectGroups().SerializeTo(element.AddChild("objectsGroups"));
  GetVariables().SerializeTo(element.AddChild("variables"));

  element.SetAttribute("firstLayout", firstLayout);
  gd::SerializerElement& layoutsElement = element.AddChild("layouts");
  layoutsElement.ConsiderAsArrayOf("layout");
  for (std::size_t i = 0; i < GetLayoutsCount(); i++)
    GetLayout(i).SerializeTo(layoutsElement.AddChild("layout"));

  SerializerElement& externalEventsElement = element.AddChild("externalEvents");
  externalEventsElement.ConsiderAsArrayOf("externalEvents");
  for (std::size_t i = 0; i < GetExternalEventsCount(); ++i)
    GetExternalEvents(i).SerializeTo(
        externalEventsElement.AddChild("externalEvents"));

  SerializerElement& eventsFunctionsExtensionsElement =
      element.AddChild("eventsFunctionsExtensions");
  eventsFunctionsExtensionsElement.ConsiderAsArrayOf(
      "eventsFunctionsExtension");
  for (std::size_t i = 0; i < eventsFunctionsExtensions.size(); ++i)
    eventsFunctionsExtensions[i]->SerializeTo(
        eventsFunctionsExtensionsElement.AddChild("eventsFunctionsExtension"));

  SerializerElement& externalLayoutsElement =
      element.AddChild("externalLayouts");
  externalLayoutsElement.ConsiderAsArrayOf("externalLayout");
  for (std::size_t i = 0; i < externalLayouts.size(); ++i)
    externalLayouts[i]->SerializeTo(
        externalLayoutsElement.AddChild("externalLayout"));
}

bool Project::IsNameSafe(const gd::String& name) {
  if (name.empty()) return false;

  if (isdigit(name[0])) return false;

  for (auto character : name) {
    if (!GrammarTerminals::IsAllowedInIdentifier(character)) {
      return false;
    }
  }

  return true;
}

gd::String Project::GetSafeName(const gd::String& name) {
  if (name.empty()) return "Unnamed";

  gd::String newName = name;

  if (isdigit(name[0])) newName = "_" + newName;

  for (size_t i = 0; i < newName.size(); ++i) {
    // Note that iterating on the characters is not super efficient (O(n^2),
    // which could be avoided with an iterator), but this function is not
    // critical for performance (only used to generate a name when a user
    // creates a new entity or rename one).
    auto character = newName[i];
    bool isAllowed = GrammarTerminals::IsAllowedInIdentifier(character);

    // Replace all unallowed letters by an underscore.
    if (!isAllowed) {
      newName.replace(i, 1, '_');
    }
  }

  return newName;
}

Project::Project(const Project &other)
    : objectsContainer(gd::ObjectsContainer::SourceType::Global) {
  Init(other);
}

Project& Project::operator=(const Project& other) {
  if (this != &other) Init(other);

  return *this;
}

void Project::Init(const gd::Project& game) {
  name = game.name;
  categories = game.categories;
  description = game.description;
  firstLayout = game.firstLayout;
  version = game.version;
  windowWidth = game.windowWidth;
  windowHeight = game.windowHeight;
  maxFPS = game.maxFPS;
  minFPS = game.minFPS;
  verticalSync = game.verticalSync;
  scaleMode = game.scaleMode;
  pixelsRounding = game.pixelsRounding;
  adaptGameResolutionAtRuntime = game.adaptGameResolutionAtRuntime;
  sizeOnStartupMode = game.sizeOnStartupMode;
  antialiasingMode = game.antialiasingMode;
  isAntialisingEnabledOnMobile = game.isAntialisingEnabledOnMobile;
  projectUuid = game.projectUuid;
  useDeprecatedZeroAsDefaultZOrder = game.useDeprecatedZeroAsDefaultZOrder;

  author = game.author;
  authorIds = game.authorIds;
  authorUsernames = game.authorUsernames;
  isPlayableWithKeyboard = game.isPlayableWithKeyboard;
  isPlayableWithGamepad = game.isPlayableWithGamepad;
  isPlayableWithMobile = game.isPlayableWithMobile;
  packageName = game.packageName;
  templateSlug = game.templateSlug;
  orientation = game.orientation;
  folderProject = game.folderProject;
  latestCompilationDirectory = game.latestCompilationDirectory;
  platformSpecificAssets = game.platformSpecificAssets;
  loadingScreen = game.loadingScreen;
  watermark = game.watermark;

  extensionProperties = game.extensionProperties;

  gdMajorVersion = game.gdMajorVersion;
  gdMinorVersion = game.gdMinorVersion;
  gdBuildVersion = game.gdBuildVersion;

  currentPlatform = game.currentPlatform;
  platforms = game.platforms;

  resourcesManager = game.resourcesManager;

  objectsContainer = game.objectsContainer;

  scenes = gd::Clone(game.scenes);

  externalEvents = gd::Clone(game.externalEvents);

  externalLayouts = gd::Clone(game.externalLayouts);
  eventsFunctionsExtensions = gd::Clone(game.eventsFunctionsExtensions);

  variables = game.GetVariables();

  projectFile = game.GetProjectFile();

  sceneResourcesPreloading = game.sceneResourcesPreloading;
  sceneResourcesUnloading = game.sceneResourcesUnloading;
}

}  // namespace gd
