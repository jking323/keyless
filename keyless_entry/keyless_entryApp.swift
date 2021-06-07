//
//  keyless_entryApp.swift
//  keyless_entry
//
//  Created by Jeremy King on 6/2/21.
//

import SwiftUI


//let keygen = key
@main
struct keyless_entryApp: App {
    let persistenceController = PersistenceController.shared

    var body: some view {
        WindowGroup {
            ContentView()
                .environment(\.managedObjectContext, persistenceController.container.viewContext)
        }
    }
}

let keygen = SecKey
