// Copyright 1996-2022 Cyberbotics Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "WbUrl.hpp"

#include "WbApplicationInfo.hpp"
#include "WbField.hpp"
#include "WbFileUtil.hpp"
#include "WbLog.hpp"
#include "WbMFString.hpp"
#include "WbNetwork.hpp"
#include "WbNode.hpp"
#include "WbNodeUtilities.hpp"
#include "WbProject.hpp"
#include "WbProtoManager.hpp"
#include "WbProtoModel.hpp"
#include "WbStandardPaths.hpp"
#include "WbWorld.hpp"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>

const QString &WbUrl::missingTexture() {
  const static QString missingTexture = WbStandardPaths::resourcesPath() + "images/missing_texture.png";
  return missingTexture;
}

const QString &WbUrl::missingProtoIcon() {
  const static QString missingProtoIcon = WbStandardPaths::resourcesPath() + "images/missing_proto_icon.png";
  return missingProtoIcon;
}

const QString WbUrl::missing(const QString &url) {
  const QString suffix = url.mid(url.lastIndexOf('.') + 1).toLower();
  const QStringList textureSuffixes = {"png", "jpg", "jpeg"};
  if (textureSuffixes.contains(suffix, Qt::CaseInsensitive))
    return missingTexture();

  return "";
}

QString WbUrl::computePath(const WbNode *node, const QString &field, const WbMFString *urlField, int index) {
  // check if mUrl is empty
  if (urlField->size() < 1)
    return "";

  // get the URL at specified index
  const QString &url = urlField->item(index);
  return computePath(node, field, url);
}

QString WbUrl::computePath(const WbNode *node, const QString &field, const QString &rawUrl) {
  QString url = resolveUrl(rawUrl);
  // check if the first URL is empty
  if (url.isEmpty()) {
    if (node)
      node->parsingWarn(QObject::tr("First item of '%1' field is empty.").arg(field));
    else
      WbLog::warning(QObject::tr("Missing '%1' value.").arg(field), false, WbLog::PARSING);
    return missing(url);
  }

  // note: web urls need to be checked first, otherwise it would also pass the isRelativePath() condition
  if (isWeb(url))
    return url;

  if (QDir::isRelativePath(url)) {
    qDebug() << "#####################################################################################" << url;

    QString parentUrl;

    assert(node);
    const WbField *f = node->findField(field, true);
    const WbNode *ip = node->containingProto(false);
    const WbNode *op = ip ? ip->containingProto(true) : NULL;

    if (ip && op) {
      qDebug() << "IP && OP";
      QString innerScope, outerScope;

      assert(ip->proto());
      innerScope = ip->proto()->url();

      if (f->parameter()) {
        f = f->parameter();
        qDebug() << "FIELD" << f->name() << ", IP" << (ip ? ip->modelName() : "NULL") << ", OP"
                 << (op ? op->modelName() : "NULL");

        while (f) {
          if (!f->parameter())
            break;

          f = f->parameter();
          ip = op;
          op = op->containingProto(true);

          qDebug() << "FIELD" << f->name() << ", IP" << (ip ? ip->modelName() : "NULL") << ", OP"
                   << (op ? op->modelName() : "NULL");
        }

        assert(f);
        assert(ip);
        assert(ip->proto());

        innerScope = ip->proto()->url();
        outerScope = op ? op->proto()->url() : WbWorld::instance()->fileName();

        qDebug() << ">> INNER SCOPE:" << QFileInfo(innerScope).fileName();
        qDebug() << ">> OUTER SCOPE:" << QFileInfo(outerScope).fileName();
        qDebug() << ">> PARAM      :" << f->name() << "DEFAULT?" << f->isDefault();

        if (!f || (f && f->isDefault())) {
          parentUrl = innerScope;
        } else {
          parentUrl = outerScope;
        }

      } else {
        qDebug() << "NO PARAMETER, AND INTERNAL";
        parentUrl = ip->proto()->url();
      }
    } else {
      if (ip) {
        assert(ip->proto());

        qDebug() << "ONLY IP" << ip->modelName();

        qDebug() << "  F" << f->name() << "DEFAULT?" << f->isDefault();
        if (f->parameter())
          qDebug() << "F PARAM" << f->parameter()->name() << "DEFAULT?" << f->parameter()->isDefault();

        if (f->parentNode() && f->parentNode()->protoParameterNode()) {
          const WbNode *ppn = f->parentNode()->protoParameterNode();

          while (ppn->parentNode()->isProtoParameterNode()) {
            ppn = ppn->parentNode();
          }

          f = ppn->parentField();
          qDebug() << "REACHED" << f->name();
        }

        if (f->isParameter()) {
          qDebug() << ">> VARIANT 0";
          parentUrl = ip->proto()->url();
        } else if (!f->parameter()) {
          qDebug() << ">> VARIANT 1";
          parentUrl = ip->proto()->url();
        } else if (f->parameter() && f->parameter()->isDefault()) {
          qDebug() << ">> VARIANT 2";
          parentUrl = ip->proto()->url();
        } else {
          qDebug() << ">> VARIANT 3";

          parentUrl = WbWorld::instance()->fileName();
        }
      } else {
        qDebug() << "NEITHER IP NOR OP";
        parentUrl = WbWorld::instance()->fileName();
      }
    }
    /*
    const WbField *f = node->findField(field, true);
    assert(f);
    qDebug() << "1. PARAM CHAIN, START FROM " << field << f;
    while (f->parameter()) {
      qDebug() << "  F JUMP FROM" << f->name() << f << "TO" << f->parameter()->name() << f->parameter();
      f = f->parameter();
    }

    qDebug() << "2. AT" << f->name() << "IS PARAM" << f->isParameter() << "IS DEFAULT" << f->isDefault() << "PARAM"
             << f->parameter() << "VISIBLE" << WbNodeUtilities::isVisible(f);

    WbNode *n = f->parentNode();
    if (WbNodeUtilities::isVisible(f) && n->protoParameterNode()) {
      qDebug() << "  NOT A PARAM, but has a PPM";

      qDebug() << "====" << n->modelName();
      if (!f->isParameter()) {
        n = n->protoParameterNode();
        qDebug() << "====" << n->modelName();
      }

      while (n->parentNode()->isProtoParameterNode()) {
        qDebug() << "====" << n->modelName();
        // search for the topmost node that has a parent which is a PROTO parameter node
        qDebug() << " NJ" << n->modelName();
        n = n->parentNode();
      }

      qDebug() << " STOP JUMP AT" << n->modelName();

      WbField *pf = n->parentField();

      qDebug() << "3. AT" << pf->name() << "IS PARAM" << pf->isParameter() << "IS DEFAULT" << pf->isDefault() << "PARAM"
               << pf->parameter();

      f = pf;
    }

    qDebug() << f->name() << "DEFAULT?" << f->isDefault();
    qDebug() << f->name() << "DEFAULT SCOPE: " << f->defaultScope();
    qDebug() << f->name() << "NON-DEFAULT SCOPE: " << f->nonDefaultScope();
    */
    // const QString &parentUrl = f->isDefault() ? f->defaultScope() : f->nonDefaultScope();
    qDebug() << "==> SCOPE:" << QFileInfo(parentUrl).fileName();
    url = combinePaths(url, parentUrl);
  }

  if (isWeb(url) || QFileInfo(url).exists())
    return url;

  return missing(rawUrl);
}

QString WbUrl::resolveUrl(const QString &rawUrl) {
  if (rawUrl.isEmpty())
    return rawUrl;

  QString url = rawUrl;
  url.replace("\\", "/");

  if (isWeb(url))
    return url;

  if (isLocalUrl(url))
    return QDir::cleanPath(url.replace("webots://", WbStandardPaths::webotsHomePath()));

  return QDir::cleanPath(url);
}

QString WbUrl::exportResource(const WbNode *node, const QString &url, const QString &sourcePath,
                              const QString &relativeResourcePath, const WbWriter &writer, const bool isTexture) {
  const QFileInfo urlFileInfo(url);
  const QString fileName = urlFileInfo.fileName();
  const QString expectedRelativePath = relativeResourcePath + fileName;
  const QString expectedPath = writer.path() + "/" + expectedRelativePath;

  if (expectedPath == sourcePath)  // everything is fine, the file is where we expect it
    return expectedPath;

  // otherwise, we need to copy it
  // but first, we need to check that folders exists and create them if they don't exist
  const QFileInfo fi(expectedPath);
  if (!QDir(fi.path()).exists())
    QDir(writer.path()).mkpath(fi.path());

  if (QFile::exists(expectedPath)) {
    if (WbFileUtil::areIdenticalFiles(sourcePath, expectedPath))
      return expectedRelativePath;
    else {
      const QString baseName = urlFileInfo.completeBaseName();
      const QString extension = urlFileInfo.suffix();

      for (int i = 1; i < 100; ++i) {  // number of trials before failure
        QString newRelativePath;
        if (isTexture)
          newRelativePath = writer.relativeTexturesPath() + baseName + '.' + QString::number(i) + '.' + extension;
        else
          newRelativePath = writer.relativeMeshesPath() + baseName + '.' + QString::number(i) + '.' + extension;

        const QString newAbsolutePath = writer.path() + "/" + newRelativePath;
        if (QFileInfo(newAbsolutePath).exists()) {
          if (WbFileUtil::areIdenticalFiles(sourcePath, newAbsolutePath))
            return newRelativePath;
        } else {
          QFile::copy(sourcePath, newAbsolutePath);
          return newRelativePath;
        }
      }
      if (isTexture)
        node->warn(QObject::tr("Failure exporting texture, too many textures share the same name: %1.").arg(url));
      else
        node->warn(QObject::tr("Failure exporting mesh, too many meshes share the same name: %1.").arg(url));

      return "";
    }
  } else {  // simple case
    QFile::copy(sourcePath, expectedPath);
    return expectedRelativePath;
  }
}

QString WbUrl::exportTexture(const WbNode *node, const WbMFString *urlField, int index, const WbWriter &writer) {
  // in addition to writing the node, we want to ensure that the texture file exists
  // at the expected location. If not, we should copy it, possibly creating the expected
  // directory structure.
  return exportResource(node, QDir::fromNativeSeparators(urlField->item(index)), computePath(node, "url", urlField, index),
                        writer.relativeTexturesPath(), writer);
}

QString WbUrl::exportMesh(const WbNode *node, const WbMFString *urlField, int index, const WbWriter &writer) {
  // in addition to writing the node, we want to ensure that the mesh file exists
  // at the expected location. If not, we should copy it, possibly creating the expected
  // directory structure.
  return exportResource(node, QDir::fromNativeSeparators(urlField->item(index)), computePath(node, "url", urlField, index),
                        writer.relativeMeshesPath(), writer, false);
}

bool WbUrl::isWeb(const QString &url) {
  return url.startsWith("https://") || url.startsWith("http://");
}

bool WbUrl::isLocalUrl(const QString &url) {
  return url.startsWith("webots://") || url.startsWith(WbStandardPaths::webotsHomePath());
}

const QString WbUrl::computeLocalAssetUrl(QString url) {
  if (!WbApplicationInfo::repo().isEmpty() && !WbApplicationInfo::branch().isEmpty()) {
    // when streaming locally, build the URL from branch.txt in order to serve 'webots://' assets
    const QString prefix =
      "https://raw.githubusercontent.com/" + WbApplicationInfo::repo() + "/" + WbApplicationInfo::branch() + "/";
    return url.replace("webots://", prefix).replace(WbStandardPaths::webotsHomePath(), prefix);
  }

  // when streaming from a distribution or nightly build, use the actual url
  return url;
}

const QString WbUrl::computePrefix(const QString &rawUrl) {
  const QString url = WbFileUtil::isLocatedInDirectory(rawUrl, WbStandardPaths::cachedAssetsPath()) ?
                        WbNetwork::instance()->getUrlFromEphemeralCache(rawUrl) :
                        rawUrl;

  if (isWeb(url)) {
    QRegularExpression re(remoteWebotsAssetRegex(true));
    QRegularExpressionMatch match = re.match(url);
    if (match.hasMatch())
      return match.captured(0);
  }

  return QString();
}

const QString WbUrl::remoteWebotsAssetRegex(bool capturing) {
  static QString regex = "https://raw.githubusercontent.com/cyberbotics/webots/[a-zA-Z0-9\\_\\-\\+]+/";
  return capturing ? "(" + regex + ")" : regex;
}

const QString &WbUrl::remoteWebotsAssetPrefix() {
  static QString url;
  if (url.isEmpty())
    // if it's an official release, use the tag (for example R2022b), if it's a nightly or local distribution use the commit
    url = "https://raw.githubusercontent.com/cyberbotics/webots/" +
          (WbApplicationInfo::commit().isEmpty() ? WbApplicationInfo::version().toString() : WbApplicationInfo::commit()) + "/";

  return url;
}

QString WbUrl::combinePaths(const QString &rawUrl, const QString &rawParentUrl) {
  // use cross-platform forward slashes
  QString url = rawUrl;
  url.replace("\\", "/");
  QString parentUrl = rawParentUrl;
  parentUrl.replace("\\", "/");

  // cases where no URL manipulation is necessary
  if (isWeb(url))
    return url;

  if (QDir::isAbsolutePath(url))
    return QDir::cleanPath(url);

  if (WbUrl::isLocalUrl(url)) {
    // URL fall-back mechanism: only trigger if the parent is a world file (.wbt), and the file (webots://) does not exist
    if (parentUrl.endsWith(".wbt", Qt::CaseInsensitive) &&
        !QFileInfo(QDir::cleanPath(url.replace("webots://", WbStandardPaths::webotsHomePath()))).exists()) {
      WbLog::error(QObject::tr("URL '%1' changed by fallback mechanism. Ensure you are opening the correct world.").arg(url));
      return url.replace("webots://", WbUrl::remoteWebotsAssetPrefix());
    }

    // infer URL based on parent's url
    const QString &prefix = WbUrl::computePrefix(parentUrl);
    if (!prefix.isEmpty())
      return url.replace("webots://", prefix);

    if (parentUrl.isEmpty() || WbUrl::isLocalUrl(parentUrl) || QDir::isAbsolutePath(parentUrl))
      return QDir::cleanPath(url.replace("webots://", WbStandardPaths::webotsHomePath()));

    return QString();
  }

  if (QDir::isRelativePath(url)) {
    // if it is not available in those folders, infer the URL based on the parent's url
    if (WbUrl::isWeb(parentUrl) || QDir::isAbsolutePath(parentUrl) || WbUrl::isLocalUrl(parentUrl)) {
      // remove filename from parent url
      parentUrl = QUrl(parentUrl).adjusted(QUrl::RemoveFilename).toString();
      if (WbUrl::isLocalUrl(parentUrl))
        parentUrl.replace("webots://", WbStandardPaths::webotsHomePath());

      if (WbUrl::isWeb(parentUrl))
        return QUrl(parentUrl).resolved(QUrl(url)).toString();
      else
        return QDir::cleanPath(QDir(parentUrl).absoluteFilePath(url));
    }
  }

  WbLog::error(QObject::tr("Impossible to infer URL from '%1' and '%2'").arg(rawUrl).arg(rawParentUrl));
  return QString();
}

QString WbUrl::expressRelativeToWorld(const QString &url) {
  return QDir(QFileInfo(WbWorld::instance()->fileName()).absolutePath()).relativeFilePath(url);
}
