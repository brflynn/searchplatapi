# Windows Search Platform APIs
Public Wrappers for Common Windows Search Service Platform APIs

These APIs assist developers in programming against the Windows Search Service in a much easier fashion than today. APIs are pretty verbose, and spread across many different header files. This takes the most commonly used functionality and puts it in a simple to use header file for Win32 application developers.

## CI/CD Workflows

This repository includes two GitHub Actions workflows:

### Pull Request Workflow
- **Trigger**: Pull requests to the `main` branch
- **Purpose**: Builds the solution and runs tests to ensure code quality
- **Steps**:
  - Restores NuGet packages
  - Builds the solution using MSBuild (Release x64)
  - Runs unit tests with VSTest

### Release Workflow
- **Trigger**: Pushes to the `release` branch
- **Purpose**: Packages header files and publishes to NuGet
- **Steps**:
  - Packages all header files from `src/api/` into a NuGet package
  - Publishes to NuGet.org (requires `NUGET_API_KEY` secret)
  - Uploads package as a build artifact

## NuGet Package

The header files are published as a NuGet package for easy consumption in C++ projects. The package includes all API headers from the `src/api/` directory.
