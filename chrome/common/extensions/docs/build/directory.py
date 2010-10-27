#!/usr/bin/python
# Copyright (c) 2010 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Class for parsing metadata about extension samples."""

import os
import os.path
import re
import hashlib
import zipfile
import simplejson as json

def parse_json_file(path, encoding="utf-8"):
  """ Load the specified file and parse it as JSON.

  Args:
    path: Path to a file containing JSON-encoded data.
    encoding: Encoding used in the file.  Defaults to utf-8.

  Returns:
    A Python object representing the data encoded in the file.

  Raises:
    Exception: If the file could not be read or its contents could not be
        parsed as JSON data.
  """
  try:
    json_file = open(path, 'r')
  except IOError, msg:
    raise Exception("Failed to read the file at %s: %s" % (path, msg))

  try:
    json_obj = json.load(json_file, encoding)
  except ValueError, msg:
    raise Exception("Failed to parse JSON out of file %s: %s" % (path, msg))
  finally:
    json_file.close()

  return json_obj

class ApiManifest(object):
  """ Represents the list of API methods contained in extension_api.json """

  _MODULE_DOC_KEYS = ['functions', 'events']
  """ Keys which may be passed to the _parseModuleDocLinksByKey method."""

  def __init__(self, manifest_path):
    """ Read the supplied manifest file and parse its contents.

    Args:
      manifest_path: Path to extension_api.json
    """
    self._manifest = parse_json_file(manifest_path)

  def _getDocLink(self, method, hashprefix):
    """
    Given an API method, return a partial URL corresponding to the doc
    file for that method.

    Args:
      method: A string like 'chrome.foo.bar' or 'chrome.experimental.foo.onBar'
      hashprefix: The prefix to put in front of hash links - 'method' for
          methods and 'event' for events.

    Returns:
      A string like 'foo.html#method-bar' or 'experimental.foo.html#event-onBar'
    """
    urlpattern = '%%s.html#%s-%%s' % hashprefix
    urlparts = tuple(method.replace('chrome.', '').rsplit('.', 1))
    return urlpattern % urlparts

  def _parseModuleDocLinksByKey(self, module, key):
    """
    Given a specific API module, returns a dict of methods or events mapped to
    documentation URLs.

    Args:
      module: The data in extension_api.json corresponding to a single module.
      key: A key belonging to _MODULE_DOC_KEYS to determine which set of
          methods to parse, and what kind of documentation URL to generate.

    Returns:
      A dict of extension methods mapped to file and hash URL parts for the
      corresponding documentation links, like:
        {
          "chrome.tabs.remove": "tabs.html#method-remove",
          "chrome.tabs.onDetached" : "tabs.html#event-onDetatched"
        }

      If the API namespace is defined "nodoc" then an empty dict is returned.

    Raises:
      Exception: If the key supplied is not a member of _MODULE_DOC_KEYS.
    """
    methods = []
    api_dict = {}
    namespace = module['namespace']
    if module.has_key('nodoc'):
      return api_dict
    if key not in self._MODULE_DOC_KEYS:
      raise Exception("key %s must be one of %s" % (key, self._MODULE_DOC_KEYS))
    if module.has_key(key):
      methods.extend(module[key])
    for method in methods:
      method_name = 'chrome.%s.%s' % (namespace, method['name'])
      hashprefix = 'method'
      if key == 'events':
        hashprefix = 'event'
      api_dict[method_name] = self._getDocLink(method_name, hashprefix)
    return api_dict

  def getModuleNames(self):
    """ Returns the names of individual modules in the API.

    Returns:
      The namespace """
    # Exclude modules with a "nodoc" property.
    return set(module['namespace'].encode() for module in self._manifest
               if "nodoc" not in module)

  def getDocumentationLinks(self):
    """ Parses the extension_api.json manifest and returns a dict of all
    events and methods for every module, mapped to relative documentation links.

    Returns:
      A dict of methods/events => partial doc links for every module.
    """
    api_dict = {}
    for module in self._manifest:
      api_dict.update(self._parseModuleDocLinksByKey(module, 'functions'))
      api_dict.update(self._parseModuleDocLinksByKey(module, 'events'))
    return api_dict

class SamplesManifest(object):
  """ Represents a manifest file containing information about the sample
  extensions available in the codebase. """

  def __init__(self, base_sample_path, base_dir, api_manifest):
    """ Reads through the filesystem and obtains information about any Chrome
    extensions which exist underneath the specified folder.

    Args:
      base_sample_path: The directory under which to search for samples.
      base_dir: The base directory samples will be referenced from.
      api_manifest: An instance of the ApiManifest class, which will indicate
          which API methods are available.
    """
    self._base_dir = base_dir
    manifest_paths = self._locateManifestsFromPath(base_sample_path)
    self._manifest_data = self._parseManifestData(manifest_paths, api_manifest)

  def _locateManifestsFromPath(self, path):
    """
    Returns a list of paths to sample extension manifest.json files.

    Args:
      base_path: Base path in which to start the search.
    Returns:
      A list of paths below base_path pointing at manifest.json files.
    """
    manifest_paths = []
    for root, directories, files in os.walk(path):
      if 'manifest.json' in files:
        directories = []             # Don't go any further down this tree
        manifest_paths.append(os.path.join(root, 'manifest.json'))
      if '.svn' in directories:
        directories.remove('.svn')   # Don't go into SVN metadata directories
    return manifest_paths

  def _parseManifestData(self, manifest_paths, api_manifest):
    """ Returns metadata about the sample extensions given their manifest
    paths.

    Args:
      manifest_paths: A list of paths to extension manifests
      api_manifest: An instance of the ApiManifest class, which will indicate
          which API methods are available.

    Returns:
      Manifest data containing a list of samples and available API methods.
    """
    api_method_dict = api_manifest.getDocumentationLinks()
    api_methods = api_method_dict.keys()

    samples = []
    for path in manifest_paths:
      sample = Sample(path, api_methods, self._base_dir)
      # Don't render apps
      if sample.is_app() == False:
        samples.append(sample)

    def compareSamples(sample1, sample2):
      """ Compares two samples as a sort comparator, by name then path. """
      value = cmp(sample1['name'].upper(), sample2['name'].upper())
      if value == 0:
        value = cmp(sample1['path'], sample2['path'])
      return value

    samples.sort(compareSamples)

    manifest_data = {'samples': samples, 'api': api_method_dict}
    return manifest_data

  def writeToFile(self, path):
    """ Writes the contents of this manifest file as a JSON-encoded text file.
    For each sample written to the manifest, create a zip file with the sample
    contents in the sample's parent directory.

    Args:
      path: The path to write the samples manifest file to.
    """

    for sample in self._manifest_data['samples']:
      sample.write_zip()

    manifest_text = json.dumps(self._manifest_data, indent=2)
    output_path = os.path.realpath(path)
    try:
      output_file = open(output_path, 'w')
    except IOError, msg:
      raise Exception("Failed to write the samples manifest file."
                      "The specific error was: %s." % msg)
    output_file.write(manifest_text)
    output_file.close()

class Sample(dict):
  """ Represents metadata about a Chrome extension sample.

  Extends dict so that it can be easily JSON serialized.
  """

  def __init__(self, manifest_path, api_methods, base_dir):
    """ Initializes a Sample instance given a path to a manifest.

    Args:
      manifest_path: A filesystem path to a manifest file.
      api_methods: A list of strings containing all possible Chrome extension
          API calls.
      base_dir: The base directory where this sample will be referenced from -
          paths will be made relative to this directory.
    """
    self._base_dir = base_dir
    self._manifest_path = manifest_path
    self._manifest = parse_json_file(self._manifest_path)
    self._locale_data = self._parse_locale_data()

    # The following properties will be serialized when converting this object
    # to JSON.

    self['api_calls'] = self._parse_api_calls(api_methods)
    self['name'] = self._parse_name()
    self['description'] = self._parse_description()
    self['icon'] = self._parse_icon()
    self['features'] = self._parse_features()
    self['protocols'] = self._parse_protocols()
    self['path'] = self._get_relative_path()
    self['search_string'] = self._get_search_string()
    self['source_files'] = self._parse_source_files()
    self['id'] = hashlib.sha1(self['path']).hexdigest()

  _FEATURE_ATTRIBUTES = (
    'browser_action',
    'page_action',
    'background_page',
    'options_page',
    'plugins',
    'theme',
    'chrome_url_overrides'
  )
  """ Attributes that will map to "features" if their corresponding key is
  present in the extension manifest. """

  _SOURCE_FILE_EXTENSIONS = ('.html', '.json', '.js', '.css', '.htm')
  """ File extensions to files which may contain source code."""

  _ENGLISH_LOCALES = ['en_US', 'en', 'en_GB']
  """ Locales from which translations may be used in the sample gallery. """

  def _get_localized_manifest_value(self, key):
    """ Returns a localized version of the requested manifest value.

    Args:
      key: The manifest key whose value the caller wants translated.

    Returns:
      If the supplied value exists and contains a ___MSG_token___ value, this
      method will resolve the appropriate translation and return the result.
      If no token exists, the manifest value will be returned.  If the key does
      not exist, an empty string will be returned.

    Raises:
      Exception: If the localized value for the given token could not be found.
    """
    if self._manifest.has_key(key):
      if self._manifest[key][:6] == '__MSG_':
        try:
          return self._get_localized_value(self._manifest[key])
        except Exception, msg:
          raise Exception("Could not translate manifest value for key %s: %s" %
                          (key, msg))
      else:
        return self._manifest[key]
    else:
      return ''

  def _get_localized_value(self, message_token):
    """ Returns the localized version of the requested MSG bundle token.

    Args:
      message_token: A message bundle token like __MSG_extensionName__.

    Returns:
      The translated text corresponding to the token, with any placeholders
      automatically resolved and substituted in.

    Raises:
      Exception: If a message bundle token is not found in the translations.
    """
    placeholder_pattern = re.compile('\$(\w*)\$')
    token = message_token[6:-2]
    if self._locale_data.has_key(token):
      message = self._locale_data[token]['message']

      placeholder_match = placeholder_pattern.search(message)
      if placeholder_match:
        # There are placeholders in the translation - substitute them.
        placeholder_name = placeholder_match.group(1)
        placeholders = self._locale_data[token]['placeholders']
        if placeholders.has_key(placeholder_name.lower()):
          placeholder_value = placeholders[placeholder_name.lower()]['content']
          placeholder_token = '$%s$' % placeholder_name
          message = message.replace(placeholder_token, placeholder_value)
      return message
    else:
      raise Exception('Could not find localized string: %s' % message_token)

  def _get_relative_path(self):
    """ Returns a relative path from the supplied base dir to the manifest dir.

    This method is used because we may not be able to rely on os.path.relpath
    which was introduced in Python 2.6 and only works on Windows and Unix.

    Since the example extensions should always be subdirectories of the
    base sample manifest path, we can get a relative path through a simple
    string substitution.

    Returns:
      A relative directory path from the sample manifest's directory to the
      directory containing this sample's manifest.json.
    """
    real_manifest_path = os.path.realpath(self._manifest_path)
    real_base_path = os.path.realpath(self._base_dir)
    return real_manifest_path.replace(real_base_path, '')\
                             .replace('manifest.json', '')[1:]

  def _get_search_string(self):
    """ Constructs a string to be used when searching the samples list.

    To make the implementation of the JavaScript-based search very direct, a
    string is constructed containing the title, description, API calls, and
    features that this sample uses, and is converted to uppercase.  This makes
    JavaScript sample searching very fast and easy to implement.

    Returns:
      An uppercase string containing information to match on for searching
      samples on the client.
    """
    search_terms = [
      self['name'],
      self['description'],
    ]
    search_terms.extend(self['features'])
    search_terms.extend(self['api_calls'])
    search_string = ' '.join(search_terms).replace('"', '')\
                                          .replace('\'', '')\
                                          .upper()
    return search_string

  def _parse_api_calls(self, api_methods):
    """ Returns a list of Chrome extension API calls the sample makes.

    Parses any *.html and *.js files in the sample directory and matches them
    against the supplied list of all available API methods, returning methods
    which show up in the sample code.

    Args:
      api_methods: A list of strings containing the potential
          API calls the and the extension sample could be making.

    Returns:
      A set of every member of api_methods that appears in any *.html or *.js
      file contained in this sample's directory (or subdirectories).

    Raises:
      Exception: If any of the *.html or *.js files cannot be read.
    """
    api_calls = set()
    extension_dir_path = os.path.dirname(self._manifest_path)
    for root, dirs, files in os.walk(extension_dir_path):
      for file in files:
        if file[-5:] == '.html' or file[-3:] == '.js':
          path = os.path.join(root, file)
          try:
            code_file = open(path)
          except IOError, msg:
            raise Exception("Failed to read %s: %s" % (path, msg))
          code_contents = code_file.read()
          code_file.close()

          for method in api_methods:
            if (code_contents.find(method) > -1):
              api_calls.add(method)
    return sorted(api_calls)

  def _parse_source_files(self):
    """ Returns a list of paths to source files present in the extenion.

    Returns:
      A list of paths relative to the manifest file directory.
    """
    source_paths = []
    base_path = os.path.realpath(os.path.dirname(self._manifest_path))
    for root, directories, files in os.walk(base_path):
      if '.svn' in directories:
        directories.remove('.svn')   # Don't go into SVN metadata directories

      for file_name in files:
        ext = os.path.splitext(file_name)[1]
        if ext in self._SOURCE_FILE_EXTENSIONS:
          path = os.path.realpath(os.path.join(root, file_name))
          path = path.replace(base_path, '')[1:]
          source_paths.append(path)
    return sorted(source_paths)

  def _parse_description(self):
    """ Returns a localized description of the extension.

    Returns:
      A localized version of the sample's description.
    """
    return self._get_localized_manifest_value('description')

  def _parse_features(self):
    """ Returns a list of features the sample uses.

    Returns:
      A list of features the extension uses, as determined by
      self._FEATURE_ATTRIBUTES.
    """
    features = set()
    for feature_attr in self._FEATURE_ATTRIBUTES:
      if self._manifest.has_key(feature_attr):
        features.add(feature_attr)

    if self._uses_popup():
      features.add('popup')

    if self._manifest.has_key('permissions'):
      for permission in self._manifest['permissions']:
        split = permission.split('://')
        if (len(split) == 1):
          features.add(split[0])
    return sorted(features)

  def _parse_icon(self):
    """ Returns the path to the 128px icon for this sample.

    Returns:
      The path to the 128px icon if defined in the manifest, None otherwise.
    """
    if (self._manifest.has_key('icons') and
        self._manifest['icons'].has_key('128')):
      return self._manifest['icons']['128']
    else:
      return None

  def _parse_locale_data(self):
    """ Parses this sample's locale data into a dict.

    Because the sample gallery is in English, this method only looks for
    translations as defined by self._ENGLISH_LOCALES.

    Returns:
      A dict containing the translation keys and corresponding English text
      for this extension.

    Raises:
      Exception: If the messages file cannot be read, or if it is improperly
          formatted JSON.
    """
    en_messages = {}
    extension_dir_path = os.path.dirname(self._manifest_path)
    for locale in self._ENGLISH_LOCALES:
      en_messages_path = os.path.join(extension_dir_path, '_locales', locale,
                                      'messages.json')
      if (os.path.isfile(en_messages_path)):
        break

    if (os.path.isfile(en_messages_path)):
      try:
        en_messages_file = open(en_messages_path, 'r')
      except IOError, msg:
        raise Exception("Failed to read %s: %s" % (en_messages_path, msg))
      en_messages_contents = en_messages_file.read()
      en_messages_file.close()
      try:
        en_messages = json.loads(en_messages_contents)
      except ValueError, msg:
        raise Exception("File %s has a syntax error: %s" %
                        (en_messages_path, msg))
    return en_messages

  def _parse_name(self):
    """ Returns a localized name for the extension.

    Returns:
      A localized version of the sample's name.
    """
    return self._get_localized_manifest_value('name')

  def _parse_protocols(self):
    """ Returns a list of protocols this extension requests permission for.

    Returns:
      A list of every unique protocol listed in the manifest's permssions.
    """
    protocols = []
    if self._manifest.has_key('permissions'):
      for permission in self._manifest['permissions']:
        split = permission.split('://')
        if (len(split) == 2) and (split[0] not in protocols):
          protocols.append(split[0] + "://")
    return protocols

  def _uses_background(self):
    """ Returns true if the extension defines a background page. """
    return self._manifest.has_key('background_page')

  def _uses_browser_action(self):
    """ Returns true if the extension defines a browser action. """
    return self._manifest.has_key('browser_action')

  def _uses_content_scripts(self):
    """ Returns true if the extension uses content scripts. """
    return self._manifest.has_key('content_scripts')

  def _uses_options(self):
    """ Returns true if the extension defines an options page. """
    return self._manifest.has_key('options_page')

  def _uses_page_action(self):
    """ Returns true if the extension uses a page action. """
    return self._manifest.has_key('page_action')

  def _uses_popup(self):
    """ Returns true if the extension defines a popup on a page or browser
    action. """
    has_b_popup = (self._uses_browser_action() and
                   self._manifest['browser_action'].has_key('popup'))
    has_p_popup = (self._uses_page_action() and
                   self._manifest['page_action'].has_key('popup'))
    return has_b_popup or has_p_popup

  def is_app(self):
    """ Returns true if the extension has an 'app' section in its manifest."""
    return self._manifest.has_key('app')

  def write_zip(self):
    """ Writes a zip file containing all of the files in this Sample's dir."""
    sample_path = os.path.realpath(os.path.dirname(self._manifest_path))
    sample_dirname = os.path.basename(sample_path)
    sample_parentpath = os.path.dirname(sample_path)

    zip_filename = "%s.zip" % sample_dirname
    zip_path = os.path.join(sample_parentpath, zip_filename)
    zip_file = zipfile.ZipFile(zip_path, 'w')

    try:
      for root, dirs, files in os.walk(sample_path):
        if '.svn' in dirs:
          dirs.remove('.svn')
        for file in files:
          # Absolute path to the file to be added.
          abspath = os.path.realpath(os.path.join(root, file))
          # Relative path to store the file in under the zip.
          relpath = sample_dirname + abspath.replace(sample_path, "")
          zip_file.write(abspath, relpath)

      self['zip_path'] = os.path.join(
          os.path.dirname(os.path.dirname(self._get_relative_path())),
          zip_filename)

    except RuntimeError, msg:
      raise Exception("Could not write zip at " % zip_path)
    finally:
      zip_file.close()