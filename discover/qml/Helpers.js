function initFeatured(model) {
    for(var row=0; row<model.count; row++) {
        var data = model.get(row)
        var appl = resourcesModel.resourceByPackageName(data.packageName)
        if(appl==null) {
            console.log("application ", packageName, " not found")
            continue
        }
        if(data.image==null)
            data.image = appl.screenshotUrl
        data.text = appl.name
        data.icon = appl.icon
        data.comment = appl.comment
        model.set(row, data)
    }
}


function getFeatured(model, data) {
    if(data==null)
        return
    
    for(var packageName in data) {
        var currentData = data[packageName]
        model.append({
            "text": currentData.package,
            "color": "red",
            "image": currentData.image,
            "icon": "kde",
            "comment": "&nbsp;",
            "packageName": currentData.package
        })
    }
}
