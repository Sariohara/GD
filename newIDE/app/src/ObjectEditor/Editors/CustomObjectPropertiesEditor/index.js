// @flow
import { Trans } from '@lingui/macro';
import { t } from '@lingui/macro';
import { I18n } from '@lingui/react';
import { type I18n as I18nType } from '@lingui/core';

import * as React from 'react';
import PropertiesEditor from '../../../PropertiesEditor';
import propertiesMapToSchema from '../../../PropertiesEditor/PropertiesMapToSchema';
import EmptyMessage from '../../../UI/EmptyMessage';
import { type EditorProps } from '../EditorProps.flow';
import { Column, Line } from '../../../UI/Grid';
import { getExtraObjectsInformation } from '../../../Hints';
import { getObjectTutorialIds } from '../../../Utils/GDevelopServices/Tutorial';
import AlertMessage from '../../../UI/AlertMessage';
import DismissableTutorialMessage from '../../../Hints/DismissableTutorialMessage';
import { mapFor } from '../../../Utils/MapFor';
import ObjectsEditorService from '../../ObjectsEditorService';
import Text from '../../../UI/Text';
import useForceUpdate from '../../../Utils/UseForceUpdate';
import {
  Accordion,
  AccordionHeader,
  AccordionBody,
} from '../../../UI/Accordion';
import { IconContainer } from '../../../UI/IconContainer';
import PreferencesContext from '../../../MainFrame/Preferences/PreferencesContext';
import AnimationList, {
  type AnimationListInterface,
} from '../SpriteEditor/AnimationList';
import PointsEditor from '../SpriteEditor/PointsEditor';
import CollisionMasksEditor from '../SpriteEditor/CollisionMasksEditor';
import {
  hasAnyFrame,
  getFirstAnimationFrame,
  setCollisionMaskOnAllFrames,
} from '../SpriteEditor/Utils/SpriteObjectHelper';
import { getMatchingCollisionMask } from '../SpriteEditor/CollisionMasksEditor/CollisionMaskHelper';
import ResourcesLoader from '../../../ResourcesLoader';
import ScrollView, { type ScrollViewInterface } from '../../../UI/ScrollView';
import FlatButton from '../../../UI/FlatButton';
import RaisedButton from '../../../UI/RaisedButton';
import FlatButtonWithSplitMenu from '../../../UI/FlatButtonWithSplitMenu';
import {
  ResponsiveLineStackLayout,
  LineStackLayout,
  ColumnStackLayout,
} from '../../../UI/Layout';
import { useResponsiveWindowSize } from '../../../UI/Responsive/ResponsiveWindowMeasurer';
import Add from '../../../UI/CustomSvgIcons/Add';
import Trash from '../../../UI/CustomSvgIcons/Trash';
import Edit from '../../../UI/CustomSvgIcons/ShareExternal';
import Dialog from '../../../UI/Dialog';
import HelpButton from '../../../UI/HelpButton';
import RestoreIcon from '../../../UI/CustomSvgIcons/Restore';
import SelectField from '../../../UI/SelectField';
import SelectOption from '../../../UI/SelectOption';
import NewVariantDialog from './NewVariantDialog';
import newNameGenerator from '../../../Utils/NewNameGenerator';
import {
  serializeToJSObject,
  unserializeFromJSObject,
} from '../../../Utils/Serializer';

const gd: libGDevelop = global.gd;

const styles = {
  icon: { width: 16, height: 16 },
};

export const getVariantName = (
  eventBasedObject: gdEventsBasedObject | null,
  customObjectConfiguration: gdCustomObjectConfiguration
): string =>
  eventBasedObject &&
  eventBasedObject
    .getVariants()
    .hasVariantNamed(customObjectConfiguration.getVariantName())
    ? customObjectConfiguration.getVariantName()
    : '';

export const getVariant = (
  eventBasedObject: gdEventsBasedObject,
  customObjectConfiguration: gdCustomObjectConfiguration
): gdEventsBasedObjectVariant => {
  const variantName = getVariantName(
    eventBasedObject,
    customObjectConfiguration
  );
  const variants = eventBasedObject.getVariants();
  return variantName
    ? variants.getVariant(variantName)
    : eventBasedObject.getDefaultVariant();
};

type Props = EditorProps;

const CustomObjectPropertiesEditor = (props: Props) => {
  const forceUpdate = useForceUpdate();

  const {
    objectConfiguration,
    project,
    layout,
    eventsFunctionsExtension,
    eventsBasedObject,
    object,
    objectName,
    resourceManagementProps,
    onSizeUpdated,
    onObjectUpdated,
    unsavedChanges,
    renderObjectNameField,
    isChildObject,
    onOpenEventBasedObjectVariantEditor,
    onDeleteEventsBasedObjectVariant,
  } = props;

  const { isMobile } = useResponsiveWindowSize();

  const customObjectConfiguration = gd.asCustomObjectConfiguration(
    objectConfiguration
  );
  const properties = customObjectConfiguration.getProperties();

  const propertiesSchema = propertiesMapToSchema(
    properties,
    object => object.getProperties(),
    (object, name, value) => object.updateProperty(name, value)
  );

  const extraInformation = getExtraObjectsInformation()[
    customObjectConfiguration.getType()
  ];

  const { values } = React.useContext(PreferencesContext);
  const tutorialIds = getObjectTutorialIds(customObjectConfiguration.getType());

  const customObjectExtensionName = gd.PlatformExtension.getExtensionFromFullObjectType(
    customObjectConfiguration.getType()
  );
  const customObjectExtension = project.hasEventsFunctionsExtensionNamed(
    customObjectExtensionName
  )
    ? project.getEventsFunctionsExtension(customObjectExtensionName)
    : null;
  const customObjectEventsBasedObject = project.hasEventsBasedObject(
    customObjectConfiguration.getType()
  )
    ? project.getEventsBasedObject(customObjectConfiguration.getType())
    : null;

  const animations = customObjectConfiguration.getAnimations();

  // The matching collision mask only takes the first sprite of the first
  // animation of the object. We consider this is enough to start with, and
  // the user can then edit the collision mask for further needs.
  const onCreateMatchingSpriteCollisionMask = React.useCallback(
    async () => {
      const firstSprite = getFirstAnimationFrame(animations);
      if (!firstSprite) {
        return;
      }
      const firstSpriteResourceName = firstSprite.getImageName();
      const firstAnimationResourceSource = ResourcesLoader.getResourceFullUrl(
        project,
        firstSpriteResourceName,
        {}
      );
      let matchingCollisionMask = null;
      try {
        matchingCollisionMask = await getMatchingCollisionMask(
          firstAnimationResourceSource
        );
      } catch (e) {
        console.error(
          'Unable to create a matching collision mask for the sprite, fallback to full image collision mask.',
          e
        );
      }
      setCollisionMaskOnAllFrames(animations, matchingCollisionMask);
      forceUpdate();
    },
    [animations, project, forceUpdate]
  );

  const scrollView = React.useRef<?ScrollViewInterface>(null);
  const animationList = React.useRef<?AnimationListInterface>(null);

  const [
    justAddedAnimationName,
    setJustAddedAnimationName,
  ] = React.useState<?string>(null);
  const justAddedAnimationElement = React.useRef<?any>(null);

  React.useEffect(
    () => {
      if (
        scrollView.current &&
        justAddedAnimationElement.current &&
        justAddedAnimationName
      ) {
        scrollView.current.scrollTo(justAddedAnimationElement.current);
        setJustAddedAnimationName(null);
        justAddedAnimationElement.current = null;
      }
    },
    [justAddedAnimationName]
  );

  const [pointsEditorOpen, setPointsEditorOpen] = React.useState(false);
  const [
    collisionMasksEditorOpen,
    setCollisionMasksEditorOpen,
  ] = React.useState(false);
  const [newVariantDialogOpen, setNewVariantDialogOpen] = React.useState(false);

  const editVariant = React.useCallback(
    () => {
      onOpenEventBasedObjectVariantEditor &&
        onOpenEventBasedObjectVariantEditor(
          gd.PlatformExtension.getExtensionFromFullObjectType(
            customObjectConfiguration.getType()
          ),
          gd.PlatformExtension.getObjectNameFromFullObjectType(
            customObjectConfiguration.getType()
          ),
          customObjectConfiguration.getVariantName()
        );
    },
    [customObjectConfiguration, onOpenEventBasedObjectVariantEditor]
  );

  const duplicateVariant = React.useCallback(
    (i18n: I18nType, newName: string) => {
      if (!customObjectEventsBasedObject) {
        return;
      }
      const variants = customObjectEventsBasedObject.getVariants();
      // TODO Forbid name with `::`
      const uniqueNewName = newNameGenerator(
        newName || i18n._(t`New variant`),
        tentativeNewName => variants.hasVariantNamed(tentativeNewName)
      );
      const oldVariantName = getVariantName(
        customObjectEventsBasedObject,
        customObjectConfiguration
      );
      const oldVariant = oldVariantName
        ? variants.getVariant(oldVariantName)
        : customObjectEventsBasedObject.getDefaultVariant();
      const newVariant = variants.insertNewVariant(uniqueNewName, 0);
      unserializeFromJSObject(
        newVariant,
        serializeToJSObject(oldVariant),
        'unserializeFrom',
        project
      );
      newVariant.setName(uniqueNewName);
      newVariant.setAssetStoreAssetId('');
      newVariant.setAssetStoreOriginalName('');
      customObjectConfiguration.setVariantName(uniqueNewName);
      setNewVariantDialogOpen(false);
      forceUpdate();
    },
    [
      customObjectConfiguration,
      customObjectEventsBasedObject,
      forceUpdate,
      project,
    ]
  );

  const deleteVariant = React.useCallback(
    () => {
      if (!customObjectEventsBasedObject || !onDeleteEventsBasedObjectVariant) {
        return;
      }
      const variants = customObjectEventsBasedObject.getVariants();
      const selectedVariantName = customObjectConfiguration.getVariantName();
      if (variants.hasVariantNamed(selectedVariantName)) {
        customObjectConfiguration.setVariantName('');
        const extensionName = gd.PlatformExtension.getExtensionFromFullObjectType(
          customObjectConfiguration.getType()
        );
        if (!project.hasEventsFunctionsExtensionNamed(extensionName)) {
          return;
        }
        const eventBasedExtension = project.getEventsFunctionsExtension(
          extensionName
        );
        onDeleteEventsBasedObjectVariant(
          eventBasedExtension,
          customObjectEventsBasedObject,
          variants.getVariant(selectedVariantName)
        );
        forceUpdate();
      }
    },
    [
      customObjectConfiguration,
      customObjectEventsBasedObject,
      forceUpdate,
      onDeleteEventsBasedObjectVariant,
      project,
    ]
  );

  const variantName = getVariantName(
    customObjectEventsBasedObject,
    customObjectConfiguration
  );

  return (
    <I18n>
      {({ i18n }) => (
        <>
          <ScrollView ref={scrollView}>
            <ColumnStackLayout noMargin>
              {renderObjectNameField && renderObjectNameField()}
              {tutorialIds.map(tutorialId => (
                <DismissableTutorialMessage
                  key={tutorialId}
                  tutorialId={tutorialId}
                />
              ))}
              {propertiesSchema.length ||
              (customObjectEventsBasedObject &&
                (customObjectEventsBasedObject.getObjects().getObjectsCount() ||
                  customObjectEventsBasedObject.isAnimatable())) ? (
                <React.Fragment>
                  {extraInformation ? (
                    <Line>
                      <ColumnStackLayout noMargin>
                        {extraInformation.map(({ kind, message }, index) => (
                          <AlertMessage kind={kind} key={index}>
                            {i18n._(message)}
                          </AlertMessage>
                        ))}
                      </ColumnStackLayout>
                    </Line>
                  ) : null}
                  <PropertiesEditor
                    unsavedChanges={unsavedChanges}
                    schema={propertiesSchema}
                    instances={[customObjectConfiguration]}
                    project={project}
                    resourceManagementProps={resourceManagementProps}
                  />
                  {!customObjectConfiguration.isForcedToOverrideEventsBasedObjectChildrenConfiguration() && (
                    <>
                      <Line>
                        <Column expand noMargin>
                          <Text size="block-title">Variant</Text>
                        </Column>
                      </Line>
                      <ColumnStackLayout expand noMargin>
                        <LineStackLayout>
                          <FlatButton
                            label={<Trans>Edit</Trans>}
                            leftIcon={<Edit />}
                            onClick={editVariant}
                            // Avoid to lose user changes by forcing them
                            // to duplicate these variants.
                            disabled={
                              !customObjectEventsBasedObject ||
                              // Variants from the asset store are reset when
                              // creating a new object with the same asset.
                              getVariant(
                                customObjectEventsBasedObject,
                                customObjectConfiguration
                              ).getAssetStoreAssetId() !== '' ||
                              // The default variant is reset when updating
                              // the extension.
                              (!variantName &&
                                !!customObjectExtension &&
                                customObjectExtension.getOriginName() ===
                                  'gdevelop-extension-store')
                            }
                          />
                          <FlatButton
                            label={<Trans>Duplicate</Trans>}
                            leftIcon={<Add />}
                            onClick={() => setNewVariantDialogOpen(true)}
                          />
                          <FlatButton
                            label={<Trans>Delete</Trans>}
                            leftIcon={<Trash />}
                            onClick={deleteVariant}
                          />
                        </LineStackLayout>
                        <SelectField
                          floatingLabelText={<Trans>Variant</Trans>}
                          value={variantName}
                          onChange={(e, i, value: string) => {
                            customObjectConfiguration.setVariantName(value);
                            forceUpdate();
                          }}
                        >
                          <SelectOption
                            key="default-variant"
                            value=""
                            label={t`Default`}
                          />
                          {customObjectEventsBasedObject &&
                            mapFor(
                              0,
                              customObjectEventsBasedObject
                                .getVariants()
                                .getVariantsCount(),
                              i => {
                                if (!customObjectEventsBasedObject) {
                                  return null;
                                }
                                const variant = customObjectEventsBasedObject
                                  .getVariants()
                                  .getVariantAt(i);
                                return (
                                  <SelectOption
                                    key={'variant-' + variant.getName()}
                                    value={variant.getName()}
                                    label={variant.getName()}
                                  />
                                );
                              }
                            )}
                        </SelectField>
                      </ColumnStackLayout>
                    </>
                  )}
                  {(!variantName ||
                    customObjectConfiguration.isForcedToOverrideEventsBasedObjectChildrenConfiguration()) &&
                    (customObjectEventsBasedObject &&
                      (!customObjectConfiguration.isForcedToOverrideEventsBasedObjectChildrenConfiguration() &&
                      !customObjectConfiguration.isMarkedAsOverridingEventsBasedObjectChildrenConfiguration() ? (
                        <Line alignItems="center">
                          <Column expand noMargin>
                            <Text size="block-title">Children objects</Text>
                          </Column>
                          <Column alignItems="right">
                            <FlatButton
                              label={
                                <Trans>Override children configuration</Trans>
                              }
                              onClick={() => {
                                customObjectConfiguration.setMarkedAsOverridingEventsBasedObjectChildrenConfiguration(
                                  true
                                );
                                customObjectConfiguration.clearChildrenConfiguration();
                                if (onObjectUpdated) {
                                  onObjectUpdated();
                                }
                                forceUpdate();
                              }}
                            />
                          </Column>
                        </Line>
                      ) : (
                        <>
                          <Line alignItems="center">
                            <Column expand noMargin>
                              <Text size="block-title">Children objects</Text>
                            </Column>
                            {!customObjectConfiguration.isForcedToOverrideEventsBasedObjectChildrenConfiguration() && (
                              <Column alignItems="right">
                                <FlatButton
                                  leftIcon={<RestoreIcon style={styles.icon} />}
                                  label={
                                    <Trans>
                                      Reset and hide children configuration
                                    </Trans>
                                  }
                                  onClick={() => {
                                    customObjectConfiguration.setMarkedAsOverridingEventsBasedObjectChildrenConfiguration(
                                      false
                                    );
                                    customObjectConfiguration.clearChildrenConfiguration();
                                    if (onObjectUpdated) {
                                      onObjectUpdated();
                                    }
                                    forceUpdate();
                                  }}
                                />
                              </Column>
                            )}
                          </Line>
                          {mapFor(
                            0,
                            customObjectEventsBasedObject
                              .getObjects()
                              .getObjectsCount(),
                            i => {
                              const childObject = customObjectEventsBasedObject
                                .getObjects()
                                .getObjectAt(i);
                              const childObjectConfiguration = customObjectConfiguration.getChildObjectConfiguration(
                                childObject.getName()
                              );
                              const editorConfiguration = ObjectsEditorService.getEditorConfiguration(
                                project,
                                childObjectConfiguration.getType()
                              );
                              const EditorComponent =
                                editorConfiguration.component;

                              const objectMetadata = gd.MetadataProvider.getObjectMetadata(
                                gd.JsPlatform.get(),
                                childObjectConfiguration.getType()
                              );
                              const iconUrl = objectMetadata.getIconFilename();
                              const tutorialIds = getObjectTutorialIds(
                                childObjectConfiguration.getType()
                              );
                              const enabledTutorialIds = tutorialIds.filter(
                                tutorialId =>
                                  !values.hiddenTutorialHints[tutorialId]
                              );
                              // TODO EBO: Add a protection against infinite loops in case
                              // of object cycles (thought it should be forbidden).
                              return (
                                <Accordion
                                  key={childObject.getName()}
                                  defaultExpanded
                                >
                                  <AccordionHeader>
                                    {iconUrl ? (
                                      <IconContainer
                                        src={iconUrl}
                                        alt={childObject.getName()}
                                        size={20}
                                      />
                                    ) : null}
                                    <Column expand>
                                      <Text size="block-title">
                                        {childObject.getName()}
                                      </Text>
                                    </Column>
                                  </AccordionHeader>
                                  <AccordionBody>
                                    <Column expand noMargin noOverflowParent>
                                      {enabledTutorialIds.length ? (
                                        <Line>
                                          <ColumnStackLayout expand>
                                            {tutorialIds.map(tutorialId => (
                                              <DismissableTutorialMessage
                                                key={tutorialId}
                                                tutorialId={tutorialId}
                                              />
                                            ))}
                                          </ColumnStackLayout>
                                        </Line>
                                      ) : null}
                                      <Line noMargin>
                                        <Column expand>
                                          <EditorComponent
                                            isChildObject
                                            objectConfiguration={
                                              childObjectConfiguration
                                            }
                                            project={project}
                                            layout={layout}
                                            eventsFunctionsExtension={
                                              eventsFunctionsExtension
                                            }
                                            eventsBasedObject={
                                              eventsBasedObject
                                            }
                                            resourceManagementProps={
                                              resourceManagementProps
                                            }
                                            onSizeUpdated={
                                              forceUpdate /*Force update to ensure dialog is properly positioned*/
                                            }
                                            objectName={
                                              objectName +
                                              ' ' +
                                              childObject.getName()
                                            }
                                            onObjectUpdated={onObjectUpdated}
                                            unsavedChanges={unsavedChanges}
                                          />
                                        </Column>
                                      </Line>
                                    </Column>
                                  </AccordionBody>
                                </Accordion>
                              );
                            }
                          )}
                        </>
                      )))}
                  {customObjectEventsBasedObject &&
                    customObjectEventsBasedObject.isAnimatable() && (
                      <Column expand>
                        <Text size="block-title">
                          <Trans>Animations</Trans>
                        </Text>
                        <AnimationList
                          ref={animationList}
                          animations={animations}
                          project={project}
                          layout={layout}
                          eventsFunctionsExtension={eventsFunctionsExtension}
                          eventsBasedObject={eventsBasedObject}
                          object={object}
                          objectName={objectName}
                          resourceManagementProps={resourceManagementProps}
                          onSizeUpdated={onSizeUpdated}
                          onObjectUpdated={onObjectUpdated}
                          isAnimationListLocked={false}
                          scrollView={scrollView}
                          onCreateMatchingSpriteCollisionMask={
                            onCreateMatchingSpriteCollisionMask
                          }
                        />
                      </Column>
                    )}
                </React.Fragment>
              ) : (
                <EmptyMessage>
                  <Trans>
                    There is nothing to configure for this object. You can still
                    use events to interact with the object.
                  </Trans>
                </EmptyMessage>
              )}
            </ColumnStackLayout>
          </ScrollView>
          {customObjectEventsBasedObject &&
            customObjectEventsBasedObject.isAnimatable() &&
            !isChildObject && (
              <Column noMargin>
                <ResponsiveLineStackLayout
                  justifyContent="space-between"
                  noColumnMargin
                >
                  {!isMobile ? ( // On mobile, use only 1 button to gain space.
                    <ResponsiveLineStackLayout noMargin noColumnMargin>
                      <FlatButton
                        label={<Trans>Edit collision masks</Trans>}
                        onClick={() => setCollisionMasksEditorOpen(true)}
                        disabled={!hasAnyFrame(animations)}
                      />
                      <FlatButton
                        label={<Trans>Edit points</Trans>}
                        onClick={() => setPointsEditorOpen(true)}
                        disabled={!hasAnyFrame(animations)}
                      />
                    </ResponsiveLineStackLayout>
                  ) : (
                    <FlatButtonWithSplitMenu
                      label={<Trans>Edit collision masks</Trans>}
                      onClick={() => setCollisionMasksEditorOpen(true)}
                      disabled={!hasAnyFrame(animations)}
                      buildMenuTemplate={i18n => [
                        {
                          label: i18n._(t`Edit points`),
                          disabled: !hasAnyFrame(animations),
                          click: () => setPointsEditorOpen(true),
                        },
                      ]}
                    />
                  )}
                  <RaisedButton
                    label={<Trans>Add an animation</Trans>}
                    primary
                    onClick={() => {
                      if (!animationList.current) {
                        return;
                      }
                      animationList.current.addAnimation();
                    }}
                    icon={<Add />}
                  />
                </ResponsiveLineStackLayout>
              </Column>
            )}
          {pointsEditorOpen && (
            <Dialog
              title={<Trans>Edit points</Trans>}
              actions={[
                <RaisedButton
                  key="apply"
                  label={<Trans>Apply</Trans>}
                  primary
                  onClick={() => setPointsEditorOpen(false)}
                />,
              ]}
              secondaryActions={[
                <HelpButton
                  helpPagePath="/objects/sprite/edit-points"
                  key="help"
                />,
              ]}
              onRequestClose={() => setPointsEditorOpen(false)}
              maxWidth="lg"
              flexBody
              fullHeight
              open={pointsEditorOpen}
            >
              <PointsEditor
                animations={animations}
                resourcesLoader={ResourcesLoader}
                project={project}
                onPointsUpdated={onObjectUpdated}
                onRenamedPoint={(oldName, newName) => {
                  if (!object) {
                    return;
                  }
                  if (layout) {
                    gd.WholeProjectRefactorer.renameObjectPointInScene(
                      project,
                      layout,
                      object,
                      oldName,
                      newName
                    );
                  } else if (eventsFunctionsExtension && eventsBasedObject) {
                    gd.WholeProjectRefactorer.renameObjectPointInEventsBasedObject(
                      project,
                      eventsFunctionsExtension,
                      eventsBasedObject,
                      object,
                      oldName,
                      newName
                    );
                  }
                }}
              />
            </Dialog>
          )}
          {collisionMasksEditorOpen && (
            <Dialog
              title={<Trans>Edit collision masks</Trans>}
              actions={[
                <RaisedButton
                  key="apply"
                  label={<Trans>Apply</Trans>}
                  primary
                  onClick={() => setCollisionMasksEditorOpen(false)}
                />,
              ]}
              secondaryActions={[
                <HelpButton
                  helpPagePath="/objects/sprite/collision-mask"
                  key="help"
                />,
              ]}
              maxWidth="lg"
              flexBody
              fullHeight
              onRequestClose={() => setCollisionMasksEditorOpen(false)}
              open={collisionMasksEditorOpen}
            >
              <CollisionMasksEditor
                animations={animations}
                resourcesLoader={ResourcesLoader}
                project={project}
                onMasksUpdated={onObjectUpdated}
                onCreateMatchingSpriteCollisionMask={
                  onCreateMatchingSpriteCollisionMask
                }
              />
            </Dialog>
          )}
          {newVariantDialogOpen && customObjectEventsBasedObject && (
            <NewVariantDialog
              initialName={variantName || i18n._(t`New variant`)}
              onApply={name => duplicateVariant(i18n, name)}
              onCancel={() => {
                setNewVariantDialogOpen(false);
              }}
            />
          )}
        </>
      )}
    </I18n>
  );
};

export default CustomObjectPropertiesEditor;
