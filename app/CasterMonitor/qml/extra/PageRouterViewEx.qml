import QtQuick
import QtQuick.Controls
import FluentUI.impl
import FluentUI.Controls


StackView {
    id: control
    property PageRouterEx router
    property bool skipCurrentCheck: true
    QtObject{
        id: d
        property var singletonMap: ({})
    }
    Connections{
        target: control.router
        function onSendRouter(path,uid,argument){
            var key=path+"_"+uid
            if(!control.skipCurrentCheck && currentItem && currentItem.path === path){
                return
            }
            var val = control.router.routes[path]
            if(!val){
                throw new Error(`Route '${path}' not found!`);
            }
            var url
            var singleton
            if(typeof val === 'string'){
                url = val
            }else if(typeof val === 'object'){
                url = val.url
                singleton = val.singleton
            }else{
                throw new Error(`Type val is not supported!`);
            }
            if(!singleton){
                singleton = false
            }
            var args = {path:path,url:url,argument:argument,singleton:singleton}
            if(singleton){
            var item
            if(key in d.singletonMap){
                item = d.singletonMap[key]
            }else{
                item = comp_panne.createObject(control,args)
                d.singletonMap[key] = item
            }
            args.singletonItem = item
            control.push(comp_panne_singleton,args)
            }else{
                control.push(comp_panne,args)
            }
        }

        // function onSendClose(path,uid){
        //     // 构建唯一的key来找到页面
        //     var key = path + "_" + uid;

        //     // 检查该页面是否存在于singletonMap中
        //     if (key in d.singletonMap) {
        //         // 获取对应的页面实例
        //         var item = d.singletonMap[key];

        //         // 如果是单例页面，不删除实例，只移除
        //         if (item.singleton) {
        //             // 从 StackView 中移除该页面
        //             control.pop(item);
        //         } else {
        //             // 非单例页面，移除并销毁页面实例
        //             control.pop   (item); // 从 StackView 移除
        //             if (item && item.singletonItem) {
        //                 // 销毁页面实例，释放资源
        //                 item.singletonItem.destroy();
        //                 item.singletonItem = null; // 清除引用，防止内存泄漏
        //             }
        //         }

        //         // 从singletonMap中删除该页面记录
        //         delete d.singletonMap[key];
        //     } else {
        //         console.warn("Page with key '" + key + "' not found in singletonMap.");
        //     }
        // }

    }
    Component{
        id: comp_panne_singleton
        Item{
            property string title: singletonItem.title
            property var path
            property var url
            property var argument
            property bool singleton
            property var singletonItem
            data: [singletonItem]
            StackView.onActivated: {
                singletonItem.__context.activated()
            }
            StackView.onDeactivated: {
                singletonItem.__context.deactivated()
            }
            onVisibleChanged: {
                if(visible){
                    data = [singletonItem]
                    this.updateLayout()
                }
            }
            Component.onCompleted: {
                this.updateLayout()
            }
            function updateLayout(){
                singletonItem.width = Qt.binding(function(){return width})
                singletonItem.height = Qt.binding(function(){return height})
            }
        }
    }
    Component{
        id: comp_panne
        HotLoader{
            id: loader_panne
            property string title
            property var path
            property var url
            property var argument
            property bool singleton
            property var stackView: control
            property var pageRouter: control.router
            property alias __context: context
            property var __current:{
                if(singleton){
                    return url
                }
                return visible ? url : ""
            }
            on__CurrentChanged: {
                loader_panne.setSource(__current,{context:context})
            }
            reload: function(){
                var timestamp = Date.now()
                var url = new URL(__current)
                url.searchParams.set('timestamp', timestamp);
                loader_panne.setSource(url.toString(),{context:context})
            }
            StackView.onActivated: {
                context.activated()
            }
            StackView.onDeactivated: {
                context.deactivated()
            }
            PageContext{
                id: context
                path: loader_panne.path
                url: loader_panne.url
                argument: loader_panne.argument
                singleton: loader_panne.singleton
                view: loader_panne.stackView
                router: loader_panne.pageRouter
            }
            onLoaded: {
                loader_panne.title = loader_panne.item.title
            }
        }
    }
}

