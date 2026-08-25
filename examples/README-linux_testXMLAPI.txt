Copyright 2026 Digital Envoy, Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

==============================================================================

README for C++ NetAcuity Client API - XML Example Program for Linux

Author:      Digital Envoy
Version:     7.0.0
Date:        2026

==============================================================================


The NetAcuity Server API for Linux C++ using the XML UDP protocol
requires some library dependencies for parsing the XML responses.
  - libxml2

These libraries can be obtained by running one of the following commands:

For Debian/Ubuntu systems, run this command:
  $ sudo apt-get install libxml2 libxml2-dev
For RedHat/CentOS systems, run this command:
  $ sudo dnf install libxml2 libxml2-devel


Examples of how to link these libraries can be found within "Makefile.testXMLAPI"

