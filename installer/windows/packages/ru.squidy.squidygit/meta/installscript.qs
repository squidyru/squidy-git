function Component() {
    component.addStopProcessForUpdateRequest("SquidyGit.exe");
}

Component.prototype.createOperations = function() {
    component.createOperations();

    component.addOperation(
        "CreateShortcut",
        "@TargetDir@/SquidyGit.exe",
        "@AllUsersStartMenuProgramsPath@/SquidyGit/SquidyGit.lnk",
        "workingDirectory=@TargetDir@"
    );
    component.addOperation(
        "CreateShortcut",
        "@TargetDir@/SquidyGit.exe",
        "@DesktopDir@/SquidyGit.lnk",
        "workingDirectory=@TargetDir@"
    );
}
